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
window._scene = scene; // Debug: expose for console access
scene.background = new THREE.Color(0x1a1a1a);

// Module-level variable to store inverse bind matrices from skeleton building
let _cachedBoneInverses = [];

// Reusable temporaries to reduce per-frame GC pressure
const _tmpVec3 = new THREE.Vector3();
const _tmpVec3b = new THREE.Vector3();
const _tmpVec3c = new THREE.Vector3();
const _tmpBox3 = new THREE.Box3();
const _tmpBox3b = new THREE.Box3();

// WeakMap cache for per-mesh bone indices (avoids recomputing each frame)
const _meshBoneIndexCache = new WeakMap();

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


// Store the current file's timeCodesPerSecond (default 24)
let currentTimeCodesPerSecond = 24;

// Debug visualization
let jointSpheres = [];
let weightVisualizationMaterial = null;
let originalMaterial = null;

// Joint selection state
let selectedJoint = null;
let selectedSphere = null;

// Mesh selection state
let allSceneMeshes = []; // Track all meshes in characterGroup
let meshVisibility = new Map(); // Per-mesh visibility state: Map<THREE.Mesh, boolean>
let selectedMeshObj = null; // Currently selected mesh
let bboxHelper = null; // Bounding box visualization helper

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
					const times = sampler.times;
					const values = sampler.values;

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
					const times = sampler.times;
					const values = sampler.values;

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
					const times = sampler.times;
					const values = sampler.values;

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

/**
 * Find a node in the Three.js hierarchy by matching a USD prim path.
 * E.g., path="/root/Armature/Armature_001", root.name="root" → finds Armature_001 node.
 * @param {THREE.Object3D} root - Root of the Three.js hierarchy
 * @param {string} usdPath - USD absolute prim path (e.g., "/root/Armature/Skeleton")
 * @returns {THREE.Object3D|null}
 */
function findNodeByUSDPath(root, usdPath) {
	const parts = usdPath.replace(/^\//, '').split('/').filter(p => p.length > 0);
	if (parts.length === 0) return null;

	let current = root;
	// If root's name matches the first path component, start matching from the second
	const startIdx = (current.name === parts[0]) ? 1 : 0;

	for (let i = startIdx; i < parts.length; i++) {
		const child = current.children.find(c => c.name === parts[i]);
		if (!child) {
			console.warn(`findNodeByUSDPath: could not find "${parts[i]}" under "${current.name}" (path: ${usdPath})`);
			return null;
		}
		current = child;
	}
	return current;
}

/**
 * Replace a THREE.Mesh with a THREE.SkinnedMesh in-place within its parent hierarchy.
 * Copies geometry, material, name, transform, and children.
 * @param {THREE.Mesh} mesh - The original mesh to replace
 * @returns {THREE.SkinnedMesh} The new SkinnedMesh
 */
function replaceWithSkinnedMesh(mesh) {
	const parent = mesh.parent;
	const skinnedMesh = new THREE.SkinnedMesh(mesh.geometry, mesh.material);
	skinnedMesh.name = mesh.name;
	skinnedMesh.position.copy(mesh.position);
	skinnedMesh.quaternion.copy(mesh.quaternion);
	skinnedMesh.scale.copy(mesh.scale);
	skinnedMesh.frustumCulled = false; // Skinned mesh bbox can be stale

	// Move children from original to new
	while (mesh.children.length > 0) {
		skinnedMesh.add(mesh.children[0]);
	}

	// Replace in parent
	if (parent) {
		parent.remove(mesh);
		parent.add(skinnedMesh);
	}

	return skinnedMesh;
}

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
	 * Following Blender's approach: uses bindTransform (world space) to derive
	 * bone local transforms, ensuring bone.matrixWorld = bindTransform at rest.
	 * restTransform is stored separately for fallback.
	 * Animation is expressed as deltas from bind pose (see convertUSDSkeletalAnimationsToThreeJS).
	 *
	 * @param {Object} skelNode - USD SkelNode
	 * @param {THREE.Bone} parentBone - Parent bone (null for root)
	 * @param {THREE.Matrix4} parentBindMatrix - Parent's bind transform (world space)
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

		// Determine bone's local transform from BIND transforms (Blender's approach)
		// This ensures bone.matrixWorld = bindTransform at rest, so
		// boneMatrix = bone.matrixWorld * inverse(bind) = identity at bind pose
		if (hasBindTransform) {
			// Compute local bind transform: localBind = inverse(parent_bind) * child_bind
			if (parentBone && parentBindMatrix) {
				const parentInverse = parentBindMatrix.clone().invert();
				const localBindMatrix = parentInverse.clone().multiply(bindMatrix);
				localBindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
			} else {
				// Root bone: use bind_transform directly (world space = local for root)
				bindMatrix.decompose(bone.position, bone.quaternion, bone.scale);
			}

			// Store bind-derived local transform for delta animation computation
			bone.userData.bindPosition = bone.position.clone();
			bone.userData.bindQuaternion = bone.quaternion.clone();
			bone.userData.bindScale = bone.scale.clone();
		} else if (hasRestTransform) {
			// No bind transform available - fall back to rest transform
			restMatrix.decompose(bone.position, bone.quaternion, bone.scale);
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
 * Reset skeleton bones to their bind pose transforms
 * Uses bind-derived local transforms from bone.userData (Blender-style)
 * @param {THREE.Skeleton} skeleton - The skeleton to reset
 */
function resetSkeletonToRestPose(skeleton) {
	if (!skeleton || !skeleton.bones) return;

	for (const bone of skeleton.bones) {
		// Reset to bind pose (bone.matrixWorld = bindTransform at rest)
		if (bone.userData.bindPosition) {
			bone.position.copy(bone.userData.bindPosition);
		}
		if (bone.userData.bindQuaternion) {
			bone.quaternion.copy(bone.userData.bindQuaternion);
		}
		if (bone.userData.bindScale) {
			bone.scale.copy(bone.userData.bindScale);
		}
	}

	// Update matrices
	skeleton.bones[0].updateMatrixWorld(true);
	console.log('Reset skeleton to bind pose');
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
		bone.getWorldPosition(_tmpVec3);
		sphere.position.copy(_tmpVec3);
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
				_tmpBox3b.copy(child.geometry.boundingBox);
				_tmpBox3b.applyMatrix4(child.matrixWorld);
				box.union(_tmpBox3b);
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
	sceneBounds.getCenter(_tmpVec3);
	sceneBounds.getSize(_tmpVec3b);

	// Calculate the maximum extent with some padding
	const maxDim = Math.max(_tmpVec3b.x, _tmpVec3b.y, _tmpVec3b.z);
	const padding = maxDim * 0.5;
	const frustumSize = maxDim + padding;

	// Update shadow camera frustum
	light.shadow.camera.left = -frustumSize;
	light.shadow.camera.right = frustumSize;
	light.shadow.camera.top = frustumSize;
	light.shadow.camera.bottom = -frustumSize;

	// Update near/far based on light position and scene bounds
	_tmpVec3c.copy(light.position);
	const dist = _tmpVec3c.sub(_tmpVec3).length(); // lightToCenter
	light.shadow.camera.near = Math.max(0.1, dist - maxDim);
	light.shadow.camera.far = dist + maxDim * 2;

	// Move light target to scene center (_tmpVec3 was overwritten by sub, recompute)
	sceneBounds.getCenter(_tmpVec3);
	light.target.position.copy(_tmpVec3);
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
 * Select a mesh and highlight it
 * @param {THREE.Mesh} mesh - The mesh to select
 */
function selectMesh(mesh) {
	// Deselect previous mesh
	deselectMesh();

	selectedMeshObj = mesh;

	// Update bbox helper
	updateBBoxHelper();

	// Update GUI selection
	const meshIndex = allSceneMeshes.indexOf(mesh);
	if (meshIndex >= 0) {
		animationParams.selectedMeshName = mesh.name || `Mesh_${meshIndex}`;
	}

	console.log(`Selected mesh: ${mesh.name}`);
}

/**
 * Deselect current mesh
 */
function deselectMesh() {
	selectedMeshObj = null;
	removeBBoxHelper();
	animationParams.selectedMeshName = 'None';
}

/**
 * Remove bounding box helper from scene
 */
function removeBBoxHelper() {
	if (bboxHelper) {
		scene.remove(bboxHelper);
		if (bboxHelper.geometry) bboxHelper.geometry.dispose();
		if (bboxHelper.material) bboxHelper.material.dispose();
		bboxHelper = null;
	}
}

/**
 * Compute skinned bounding box for a mesh using skeleton bone positions
 * @param {THREE.Mesh} mesh - The mesh to compute bbox for
 * @returns {THREE.Box3} The bounding box
 */
function computeSkinnedBBox(mesh, targetBox = null) {
	const box = targetBox || new THREE.Box3();
	box.makeEmpty();

	if (mesh.isSkinnedMesh && mesh.skeleton && mesh.skeleton.bones.length > 0) {
		// Use bone world positions for skinned meshes
		expandBoxBySkeletonBones(mesh.skeleton, box);
		// Add margin for mesh volume around bones
		box.getSize(_tmpVec3b);
		_tmpVec3c.copy(_tmpVec3b).multiplyScalar(0.15);
		box.min.sub(_tmpVec3c);
		box.max.add(_tmpVec3c);
	} else {
		// Regular mesh - use geometry bounds
		box.expandByObject(mesh);
	}

	return box;
}

/**
 * Expand bounding box using only the bones that influence a specific mesh
 * @param {THREE.SkinnedMesh} mesh - The skinned mesh
 * @param {THREE.Box3} box - Box to expand
 */
function expandBoxByMeshBones(mesh, box) {
	if (!mesh.skeleton || !mesh.geometry) return;

	const skinIndex = mesh.geometry.attributes.skinIndex;
	if (!skinIndex) return;

	// Use cached bone indices or compute and cache them
	let usedBoneIndices = _meshBoneIndexCache.get(mesh.geometry);
	if (!usedBoneIndices) {
		const indexSet = new Set();
		for (let i = 0; i < skinIndex.count; i++) {
			for (let j = 0; j < skinIndex.itemSize; j++) {
				indexSet.add(skinIndex.getComponent(i, j));
			}
		}
		usedBoneIndices = Array.from(indexSet); // array for faster iteration
		_meshBoneIndexCache.set(mesh.geometry, usedBoneIndices);
	}

	// Use only the bones that influence this mesh
	for (let i = 0; i < usedBoneIndices.length; i++) {
		const bone = mesh.skeleton.bones[usedBoneIndices[i]];
		if (bone) {
			bone.getWorldPosition(_tmpVec3);
			box.expandByPoint(_tmpVec3);
		}
	}
}

/**
 * Compute the current bounding box for selected mesh or all scene meshes
 * @returns {THREE.Box3} The computed bounding box
 */
function computeCurrentBBox() {
	_tmpBox3.makeEmpty();

	if (selectedMeshObj) {
		if (selectedMeshObj.isSkinnedMesh && selectedMeshObj.skeleton) {
			expandBoxByMeshBones(selectedMeshObj, _tmpBox3);
		} else {
			_tmpBox3.expandByObject(selectedMeshObj);
		}
	} else {
		for (const mesh of allSceneMeshes) {
			if (mesh.isSkinnedMesh && mesh.skeleton) {
				expandBoxBySkeletonBones(mesh.skeleton, _tmpBox3);
			} else {
				_tmpBox3.expandByObject(mesh);
			}
		}
	}

	if (!_tmpBox3.isEmpty()) {
		_tmpBox3.getSize(_tmpVec3);
		_tmpVec3b.copy(_tmpVec3).multiplyScalar(0.15);
		_tmpBox3.min.sub(_tmpVec3b);
		_tmpBox3.max.add(_tmpVec3b);
	}

	return _tmpBox3;
}

/**
 * Update bounding box helper for the selected mesh or all scene meshes
 */
function updateBBoxHelper() {
	if (!animationParams.showBBox) {
		removeBBoxHelper();
		return;
	}

	const box = computeCurrentBBox();

	if (box.isEmpty()) {
		removeBBoxHelper();
		return;
	}

	if (!bboxHelper) {
		bboxHelper = new THREE.Box3Helper(box, 0x00ff00);
		scene.add(bboxHelper);
	} else {
		// Update existing helper's box bounds
		bboxHelper.box.copy(box);
	}
}

// Track mouse down position for drag detection
let _mouseDownPos = { x: 0, y: 0 };
window.addEventListener('mousedown', (event) => {
	_mouseDownPos.x = event.clientX;
	_mouseDownPos.y = event.clientY;
});

/**
 * Raycast against skinned meshes using their deformed (post-skinning) positions.
 * Standard raycasting tests against bind-pose geometry, which fails from side views
 * when the animated pose differs significantly from the bind pose.
 *
 * @param {THREE.Raycaster} raycaster - Configured raycaster
 * @param {Array<THREE.Mesh>} meshes - Meshes to test
 * @returns {Array<{distance: number, object: THREE.Mesh}>} Sorted intersections
 */
function raycastSkinnedMeshes(raycaster, meshes) {
	const results = [];
	const _tempPos = new THREE.Vector3();
	const _bbox = new THREE.Box3();

	for (const mesh of meshes) {
		if (!mesh.visible) continue;

		if (mesh.isSkinnedMesh && mesh.skeleton) {
			// Early rejection: check ray against bone-based bounding box
			_bbox.makeEmpty();
			expandBoxByMeshBones(mesh, _bbox);
			if (_bbox.isEmpty()) continue;
			// Pad the bbox slightly for tolerance
			_bbox.expandByScalar(5);
			if (!raycaster.ray.intersectsBox(_bbox)) continue;

			// Compute skinned positions into a temporary buffer, then raycast
			const geo = mesh.geometry;
			const posAttr = geo.attributes.position;
			const indexAttr = geo.index;
			if (!posAttr) continue;

			// Create or reuse a temporary skinned position buffer
			if (!mesh._skinnedPositions || mesh._skinnedPositions.length !== posAttr.count * 3) {
				mesh._skinnedPositions = new Float32Array(posAttr.count * 3);
			}
			const skinned = mesh._skinnedPositions;

			// Compute skinned world positions using applyBoneTransform
			for (let i = 0; i < posAttr.count; i++) {
				_tempPos.fromBufferAttribute(posAttr, i);
				mesh.applyBoneTransform(i, _tempPos);
				_tempPos.applyMatrix4(mesh.matrixWorld);
				skinned[i * 3] = _tempPos.x;
				skinned[i * 3 + 1] = _tempPos.y;
				skinned[i * 3 + 2] = _tempPos.z;
			}

			// Raycast triangles against skinned positions
			const _a = new THREE.Vector3();
			const _b = new THREE.Vector3();
			const _c = new THREE.Vector3();
			const _intersectPoint = new THREE.Vector3();
			const triCount = indexAttr ? indexAttr.count / 3 : posAttr.count / 3;

			for (let t = 0; t < triCount; t++) {
				let ia, ib, ic;
				if (indexAttr) {
					ia = indexAttr.getX(t * 3);
					ib = indexAttr.getX(t * 3 + 1);
					ic = indexAttr.getX(t * 3 + 2);
				} else {
					ia = t * 3;
					ib = t * 3 + 1;
					ic = t * 3 + 2;
				}
				_a.set(skinned[ia * 3], skinned[ia * 3 + 1], skinned[ia * 3 + 2]);
				_b.set(skinned[ib * 3], skinned[ib * 3 + 1], skinned[ib * 3 + 2]);
				_c.set(skinned[ic * 3], skinned[ic * 3 + 1], skinned[ic * 3 + 2]);

				const hit = raycaster.ray.intersectTriangle(_c, _b, _a, false, _intersectPoint);
				if (hit) {
					const dist = raycaster.ray.origin.distanceTo(_intersectPoint);
					if (dist >= raycaster.near && dist <= raycaster.far) {
						results.push({ distance: dist, object: mesh, point: _intersectPoint.clone() });
						break; // One hit per mesh is enough for selection
					}
				}
			}
		} else {
			// Non-skinned mesh: use standard raycasting
			const hits = raycaster.intersectObject(mesh);
			if (hits.length > 0) {
				results.push({ distance: hits[0].distance, object: mesh, point: hits[0].point });
			}
		}
	}

	results.sort((a, b) => a.distance - b.distance);
	return results;
}

/**
 * Handle mouse click for joint and mesh selection via raycasting
 * @param {MouseEvent} event - Mouse event
 */
function onMouseClick(event) {
	// Ignore clicks on GUI elements
	if (event.target && event.target.closest && event.target.closest('.lil-gui')) return;

	// Ignore drags (user was orbiting, not clicking)
	const dx = event.clientX - _mouseDownPos.x;
	const dy = event.clientY - _mouseDownPos.y;
	if (dx * dx + dy * dy > 9) return; // 3px threshold

	// Calculate mouse position in normalized device coordinates
	mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
	mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;

	// Update raycaster
	raycaster.setFromCamera(mouse, camera);

	// Check for intersections with joint spheres first (higher priority)
	if (jointSpheres.length > 0 && animationParams.showJoints) {
		const jointIntersects = raycaster.intersectObjects(jointSpheres);
		if (jointIntersects.length > 0) {
			const sphere = jointIntersects[0].object;
			const bone = sphere.userData.bone;
			selectJoint(bone, sphere);
			return;
		}
	}

	// Check for intersections with scene meshes (using skinned positions)
	if (allSceneMeshes.length > 0) {
		const meshIntersects = raycastSkinnedMeshes(raycaster, allSceneMeshes);
		if (meshIntersects.length > 0) {
			selectMesh(meshIntersects[0].object);
			return;
		}
	}

	// Click on empty space - deselect mesh
	deselectMesh();
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

	for (const bone of skeleton.bones) {
		bone.getWorldPosition(_tmpVec3);
		box.expandByPoint(_tmpVec3);
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
		const clip = usdAnimations[animationParams.currentAnimation] || usdAnimations[0];
		const duration = clip.duration;
		const boneCount = skinnedMesh.skeleton.bones.length;
		const numSamples = boneCount > 500 ? 5 : 20; // fewer samples for large skeletons
		const savedTime = animationAction.time;

		console.log(`fitCameraToScene: Sampling ${numSamples} frames over ${duration.toFixed(2)}s animation (${boneCount} bones)`);

		for (let i = 0; i <= numSamples; i++) {
			const sampleTime = (i / numSamples) * duration;
			animationAction.time = sampleTime;
			mixer.setTime(sampleTime);
			skinnedMesh.skeleton.update();
			target.updateMatrixWorld(true);
			expandBoxBySkeletonBones(skinnedMesh.skeleton, box);
		}

		animationAction.time = savedTime;
		mixer.setTime(savedTime);
		skinnedMesh.skeleton.update();
		target.updateMatrixWorld(true);
	} else if (hasSkeleton) {
		expandBoxBySkeletonBones(skinnedMesh.skeleton, box);
	} else {
		box.expandByObject(target);
	}

	if (box.isEmpty()) {
		console.warn('fitCameraToScene: Could not compute bounding box');
		return;
	}

	// Add padding for mesh volume around bones
	box.getSize(_tmpVec3);
	_tmpVec3b.copy(_tmpVec3).multiplyScalar(0.15);
	box.min.sub(_tmpVec3b);
	box.max.add(_tmpVec3b);

	box.getCenter(_tmpVec3);   // center
	box.getSize(_tmpVec3b);    // finalSize
	const maxDim = Math.max(_tmpVec3b.x, _tmpVec3b.y, _tmpVec3b.z);

	const fov = camera.fov * (Math.PI / 180);
	const cameraDistance = (maxDim / 2) / Math.tan(fov / 2) * paddingFactor;

	_tmpVec3c.set(cameraDistance * 0.7, cameraDistance * 0.5, cameraDistance * 0.7);
	camera.position.copy(_tmpVec3).add(_tmpVec3c);
	camera.lookAt(_tmpVec3);

	controls.target.copy(_tmpVec3);
	controls.update();

	camera.near = Math.max(0.01, maxDim * 0.001);
	camera.far = maxDim * 100;
	camera.updateProjectionMatrix();

	console.log(`fitCameraToScene: center=(${_tmpVec3.x.toFixed(2)}, ${_tmpVec3.y.toFixed(2)}, ${_tmpVec3.z.toFixed(2)}), size=(${_tmpVec3b.x.toFixed(2)}, ${_tmpVec3b.y.toFixed(2)}, ${_tmpVec3b.z.toFixed(2)}), distance=${cameraDistance.toFixed(2)}${hasAnimation ? ' (sampled animation)' : ''}`);
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
	//const usd_filename = "./assets/CesiumMan.usdz";
	const usd_filename = "./assets/StandingRunForward.usdz";
	//const usd_filename = "./assets/skintest-animated.usda";

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

	// Dispose old animation mixer before clearing references
	if (mixer) {
		mixer.stopAllAction();
		mixer.uncacheRoot(mixer.getRoot());
		mixer = null;
	}
	animationAction = null;

	// Dispose all tracked meshes (geometries, materials, textures)
	for (const mesh of allSceneMeshes) {
		if (mesh.geometry) mesh.geometry.dispose();
		if (mesh.material) {
			if (Array.isArray(mesh.material)) {
				mesh.material.forEach(m => m.dispose());
			} else {
				mesh.material.dispose();
			}
		}
		if (mesh.customDepthMaterial) mesh.customDepthMaterial.dispose();
	}

	// Dispose skeleton bone textures
	if (skeleton && skeleton.boneTexture) {
		skeleton.boneTexture.dispose();
	}
	skinnedMesh = null;
	skeleton = null;

	// Dispose skeleton helper
	if (skeletonHelper) {
		scene.remove(skeletonHelper);
		if (skeletonHelper.geometry) skeletonHelper.geometry.dispose();
		if (skeletonHelper.material) {
			if (Array.isArray(skeletonHelper.material)) {
				skeletonHelper.material.forEach(m => m.dispose());
			} else {
				skeletonHelper.material.dispose();
			}
		}
		skeletonHelper = null;
	}

	// Clear all tracked meshes
	allSceneMeshes = [];
	meshVisibility.clear();
	deselectMesh();

	// Dispose and clear joint spheres
	jointSpheres.forEach(sphere => {
		if (sphere.geometry) sphere.geometry.dispose();
		if (sphere.material) sphere.material.dispose();
		scene.remove(sphere);
	});
	jointSpheres = [];

	// Deselect any selected joint
	deselectJoint();

	// Reset materials
	originalMaterial = null;
	if (weightVisualizationMaterial) {
		weightVisualizationMaterial.dispose();
		weightVisualizationMaterial = null;
	}

	// Reset animations and caches
	usdAnimations = [];
	boneMap.clear();
	_cachedBoneInverses = [];
	animationParams.hasUSDAnimations = false;
	animationParams.usdAnimationCount = 0;
	// Store loader globally for debugging
	window.usd_scene = usd_scene;

	// Reset characterGroup transforms to prevent stale state from previous loads
	characterGroup.position.set(0, 0, 0);
	characterGroup.quaternion.identity();
	characterGroup.scale.set(1, 1, 1);

	// Clear previous threeNode children from characterGroup
	while (characterGroup.children.length > 0) {
		characterGroup.remove(characterGroup.children[0]);
	}

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

	console.log(`upAxis: "${fileUpAxis}"`);
	console.log('========================');

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
	let skeletonAbsPath = null; // USD path of skeleton prim (for hierarchy placement)

	if (numSkeletons > 0) {
		// Get first skeleton (for simplicity, only support one skeleton for now)
		const usdSkeleton = usd_scene.getSkeleton(0);
		skeletonAbsPath = usdSkeleton.abs_path || null;
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

	// =====================================================================
	// Hierarchy-preserving skinning setup
	//
	// Instead of extracting meshes from threeNode and re-parenting to
	// characterGroup (which loses ancestor transforms like scale and axis
	// conversion), we:
	//   1. Add threeNode directly to characterGroup (preserves full hierarchy)
	//   2. Place rootBone at the skeleton's location in the hierarchy
	//   3. Replace Mesh → SkinnedMesh in-place within the hierarchy
	//   4. bind() with explicit geomBindTransform (prevents calculateInverses()
	//      from overwriting USD boneInverses)
	//
	// With AttachedBindMode (default), mesh.matrixWorld cancels in the vertex
	// shader, so the final world position is:
	//   world = sum(w * boneMatrix_i) * bindMatrix * pos
	// where boneMatrix_i = bone.matrixWorld * boneInverse_i
	//
	// By placing bones at the skeleton's scene graph location, bone.matrixWorld
	// includes the correct ancestor transforms (scale, axis conversion), giving
	// correct display without manual Z-up/scale handling.
	// =====================================================================

	// 1. Add threeNode to characterGroup (preserves USD scene graph hierarchy)
	characterGroup.add(threeNode);

	// Set Z-up to Y-up conversion based on file metadata
	// With AttachedBindMode, this rotation R factors out of boneMatrix:
	//   world = R * sum(w * old_boneMatrix_i) * bindMatrix * pos
	// so the entire skinned result is correctly rotated.
	animationParams.convertZUp = (fileUpAxis.toUpperCase() === 'Z');
	animationParams.toggleZUp();
	console.log(`Z-up → Y-up conversion: ${animationParams.convertZUp ? 'ON' : 'OFF'} (upAxis="${fileUpAxis}")`)

	// Collect all meshes from the hierarchy (before we start replacing them)
	const allMeshes = [];
	threeNode.traverse((child) => {
		if (child.isMesh) {
			allMeshes.push(child);
		}
	});

	console.log(`Found ${allMeshes.length} meshes in hierarchy, ${allSkinnedMeshUSDData.size} have USD skinning data`);

	if (allMeshes.length > 0 && bones.length > 0 && rootBone) {
		// 2. Create skeleton with USD inverse bind matrices
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

		// 3. Place rootBone at the skeleton's location in the threeNode hierarchy
		// This ensures bone.matrixWorld includes the correct ancestor transforms
		// (e.g., Armature's 0.01*Rx(+90°) for StandingRunForward)
		let skeletonParentNode = null;
		if (skeletonAbsPath) {
			skeletonParentNode = findNodeByUSDPath(threeNode, skeletonAbsPath);
			if (skeletonParentNode) {
				console.log(`Placing rootBone at skeleton node: ${skeletonAbsPath} (${skeletonParentNode.name})`);
			} else {
				console.warn(`Could not find skeleton node "${skeletonAbsPath}" in hierarchy, falling back to threeNode root`);
			}
		}
		// Fallback: add rootBone to threeNode root (ancestor transforms may be missing)
		const boneParent = skeletonParentNode || threeNode;
		boneParent.add(rootBone);

		// 4. Propagate all world matrices before binding
		characterGroup.updateMatrixWorld(true);

		// 5. Process each mesh: add skinning attributes and replace with SkinnedMesh
		let firstSkinnedMesh = null;
		let processedCount = 0;

		for (const mesh of allMeshes) {
			const meshName = mesh.name;
			const meshUSDData = allSkinnedMeshUSDData.get(meshName);

			if (meshUSDData) {
				console.log(`Processing mesh: ${meshName}`);

				const geometry = mesh.geometry;
				if (geometry.attributes.position) {
					const vertexCount = geometry.attributes.position.count;
					const influencesPerVertex = meshUSDData.elementSize || 4;
					const usdVertexCount = Math.floor(meshUSDData.jointIndices.length / influencesPerVertex);

					console.log(`  Adding skinning: ${vertexCount} vertices, ${influencesPerVertex} influences/vertex`);

					if (vertexCount !== usdVertexCount) {
						console.warn(`  Vertex count mismatch: Three.js=${vertexCount}, USD=${usdVertexCount}`);
					}

					// Add skinning attributes to geometry
					const skinningConfig = addExtendedSkinningAttributes(
						geometry,
						meshUSDData.jointIndices,
						meshUSDData.jointWeights,
						influencesPerVertex,
						{ normalize: true }
					);

					console.log(`  Skinning mode: ${skinningConfig.mode}`);

					// Replace Mesh with SkinnedMesh in-place within the hierarchy
					const newSkinnedMesh = replaceWithSkinnedMesh(mesh);
					newSkinnedMesh.castShadow = true;
					newSkinnedMesh.receiveShadow = true;
					newSkinnedMesh.visible = animationParams.showMesh;

					// Bind with explicit geomBindTransform
					// MUST pass bindMatrix explicitly to prevent bind() from calling
					// calculateInverses() which would overwrite USD boneInverses
					const bindMatrix = meshUSDData.geomBindTransform || new THREE.Matrix4(); // Identity if no geomBindTransform
					newSkinnedMesh.bind(skeleton, bindMatrix);
					if (meshUSDData.geomBindTransform) {
						console.log(`  Bound with geomBindTransform`);
					} else {
						console.log(`  Bound with identity bindMatrix`);
					}

					// Apply extended skinning material if needed (8+ bones per vertex)
					let wasmBoneTexture = null;
					if (animationParams.useWASMBoneTexture && meshUSDData.elementSize > 8) {
						try {
							wasmBoneTexture = usd_scene.generateBoneTexture(meshUSDData.meshId, 0);
							if (wasmBoneTexture && wasmBoneTexture.error) {
								console.warn(`WASM bone texture generation failed: ${wasmBoneTexture.error}`);
								wasmBoneTexture = null;
							}
						} catch (texErr) {
							console.warn(`WASM bone texture error: ${texErr.message}`);
							wasmBoneTexture = null;
						}
					}

					if (applyExtendedSkinningIfNeeded(newSkinnedMesh, { wasmBoneTexture })) {
						console.log(`  Extended skinning material applied`);
					}

					if (!firstSkinnedMesh) {
						firstSkinnedMesh = newSkinnedMesh;
						skinnedMesh = newSkinnedMesh;
						originalMaterial = newSkinnedMesh.material;
					}

					allSceneMeshes.push(newSkinnedMesh);
					meshVisibility.set(newSkinnedMesh, true);
					processedCount++;
				}
			} else {
				// Non-skinned mesh: stays in hierarchy as-is (threeNode is already in characterGroup)
				console.log(`Mesh ${meshName}: no USD skinning data, keeping as regular mesh`);
				mesh.castShadow = true;
				mesh.receiveShadow = true;
				mesh.visible = animationParams.showMesh;
				allSceneMeshes.push(mesh);
				meshVisibility.set(mesh, true);

				if (!skinnedMesh) {
					skinnedMesh = mesh;
					originalMaterial = mesh.material;
				}
			}
		}

		console.log(`Processed ${processedCount} skinned meshes (hierarchy preserved)`);

		if (firstSkinnedMesh) {
			// Create skeleton helper for visualization
			// Use rootBone (not firstSkinnedMesh) because bones are in the hierarchy
			// under the skeleton node, not under the skinned mesh
			skeletonHelper = new THREE.SkeletonHelper(rootBone);
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
		// No skeleton or no meshes — scene is already in characterGroup via threeNode
		console.log('No skeleton data or no meshes, scene added as-is');
		threeNode.traverse((child) => {
			if (child.isMesh) {
				child.castShadow = true;
				child.receiveShadow = true;
				child.visible = animationParams.showMesh;
				allSceneMeshes.push(child);
				meshVisibility.set(child, true);
				if (!skinnedMesh) {
					skinnedMesh = child;
					originalMaterial = child.material;
				}
			}
		});

		// Update joint hierarchy display (empty)
		if (window.updateJointHierarchy) {
			window.updateJointHierarchy(generateJointHierarchy([]));
		}
		if (typeof updateJointHierarchyGUI === 'function') {
			updateJointHierarchyGUI([]);
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

	// Update mesh list GUI
	updateMeshListGUI();

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

	// Play new animation (absolute local transforms, Normal blend mode)
	const clip = usdAnimations[index];
	animationAction = mixer.clipAction(clip);
	animationAction.loop = THREE.LoopRepeat;
	animationAction.clampWhenFinished = false;
	animationAction.setEffectiveTimeScale(animationParams.speed);
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
		case ' ': // Space = Play/Pause
			event.preventDefault();
			animationParams.playPause();
			console.log(`Animation ${animationParams.isPlaying ? 'playing' : 'paused'}`);
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
		// Toggle visibility for all tracked meshes, respecting per-mesh state
		for (const mesh of allSceneMeshes) {
			const perMesh = meshVisibility.get(mesh) !== false;
			mesh.visible = perMesh && this.showMesh;
		}
	},

	showSkeleton: false,
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

	// Z-up to Y-up conversion
	convertZUp: false,
	toggleZUp: function() {
		if (this.convertZUp) {
			characterGroup.rotation.x = -Math.PI / 2;
		} else {
			characterGroup.rotation.x = 0;
		}
	},

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

	// Mesh selection and bounding box
	selectedMeshName: 'None',
	showBBox: false,
	toggleBBox: function() {
		updateBBoxHelper();
	},
	selectMeshByIndex: function(index) {
		if (index < 0 || index >= allSceneMeshes.length) {
			deselectMesh();
		} else {
			selectMesh(allSceneMeshes[index]);
		}
	},
	toggleMeshVisibility: function(index) {
		if (index < 0 || index >= allSceneMeshes.length) return;
		const mesh = allSceneMeshes[index];
		const current = meshVisibility.get(mesh) !== false;
		const newState = !current;
		meshVisibility.set(mesh, newState);
		mesh.visible = newState && this.showMesh;
		updateMeshListGUI();
	},
	deselectMesh: function() {
		deselectMesh();
	},

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
		if (animationAction && mixer) {
			animationAction.time = animationParams.time;
			// Propagate pose even when paused
			if (!animationParams.isPlaying) {
				mixer.update(0);
			}
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
visualFolder.add(animationParams, 'convertZUp')
	.name('Z-up → Y-up')
	.onChange(() => animationParams.toggleZUp())
	.listen();
visualFolder.add(animationParams, 'fitToScene').name('Fit to Scene');
visualFolder.open();

// Mesh Selection controls
const meshFolder = gui.addFolder('Mesh Selection');
meshFolder.add(animationParams, 'selectedMeshName').name('Selected').listen().disable();
meshFolder.add(animationParams, 'showBBox')
	.name('Show BBox')
	.onChange(() => animationParams.toggleBBox());
meshFolder.add(animationParams, 'deselectMesh').name('Deselect Mesh');

// Custom container for mesh list (populated when model loads)
const meshListContainer = document.createElement('div');
meshListContainer.id = 'gui-mesh-list';
meshListContainer.style.cssText = `
	max-height: 150px;
	overflow-y: auto;
	padding: 4px 8px;
	font-family: monospace;
	font-size: 11px;
	line-height: 1.6;
	background: rgba(0, 0, 0, 0.2);
	border-radius: 3px;
	margin: 4px;
`;
meshListContainer.innerHTML = '<span style="color: #888;">No meshes loaded</span>';
try {
	if (meshFolder.$children) {
		meshFolder.$children.appendChild(meshListContainer);
	} else if (meshFolder.domElement) {
		const childrenContainer = meshFolder.domElement.querySelector('.children');
		if (childrenContainer) {
			childrenContainer.appendChild(meshListContainer);
		} else {
			meshFolder.domElement.appendChild(meshListContainer);
		}
	}
} catch (e) {
	console.warn('Could not append mesh list container to GUI:', e);
}
meshFolder.open();

/**
 * Update mesh list in GUI when model is loaded
 */
function updateMeshListGUI() {
	if (!meshListContainer) return;

	if (allSceneMeshes.length === 0) {
		meshListContainer.innerHTML = '<span style="color: #888;">No meshes loaded</span>';
		return;
	}

	let html = '';
	allSceneMeshes.forEach((mesh, index) => {
		const name = mesh.name || `Mesh_${index}`;
		const isSkinned = mesh.isSkinnedMesh ? ' [skinned]' : '';
		const vertCount = mesh.geometry?.attributes?.position?.count || 0;
		const isVisible = meshVisibility.get(mesh) !== false;
		html += `<div style="display: flex; align-items: center; padding: 2px; border-radius: 3px;"
		              class="gui-mesh-item"
		              data-mesh-index="${index}"
		              onmouseover="this.style.backgroundColor='rgba(255,255,255,0.15)'"
		              onmouseout="this.style.backgroundColor='transparent'">` +
			`<span class="gui-mesh-eye" data-mesh-index="${index}"
			       style="cursor: pointer; margin-right: 6px; font-size: 14px; user-select: none; opacity: ${isVisible ? '1' : '0.35'};"
			       title="${isVisible ? 'Hide mesh' : 'Show mesh'}">${isVisible ? '\u{1F441}' : '\u{1F6AB}'}</span>` +
			`<span class="gui-mesh-name" data-mesh-index="${index}"
			       style="color: ${isVisible ? '#6bbfff' : '#666'}; cursor: pointer; flex: 1;">${name}${isSkinned} (${vertCount}v)</span></div>`;
	});

	meshListContainer.innerHTML = html;

	// Add click handlers for mesh name (selection)
	const meshNames = meshListContainer.querySelectorAll('.gui-mesh-name');
	meshNames.forEach(item => {
		item.addEventListener('click', function() {
			const meshIndex = parseInt(this.getAttribute('data-mesh-index'));
			animationParams.selectMeshByIndex(meshIndex);
		});
	});

	// Add click handlers for eye icon (visibility toggle)
	const eyeIcons = meshListContainer.querySelectorAll('.gui-mesh-eye');
	eyeIcons.forEach(item => {
		item.addEventListener('click', function(e) {
			e.stopPropagation();
			const meshIndex = parseInt(this.getAttribute('data-mesh-index'));
			animationParams.toggleMeshVisibility(meshIndex);
		});
	});
}

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
		mixer.update(deltaTime);

		// Update time display
		if (animationAction) {
			animationParams.time = animationAction.time;
		}
	}

	// Update skeleton helper
	if (skeletonHelper && skeletonHelper.visible && typeof skeletonHelper.update === 'function') {
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

	// Update bounding box helper (needs to track skinning deformation)
	if (animationParams.showBBox && bboxHelper) {
		updateBBoxHelper();
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
