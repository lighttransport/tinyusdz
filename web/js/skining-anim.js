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
scene.add(transformControls);

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
 * @param {Object} usdSkeleton - USD skeleton data
 * @param {number} skeletonId - Index of the skeleton
 * @returns {Object} { bones: Array<THREE.Bone>, boneMap: Map }
 */
function buildSkeletonFromUSD(usdSkeleton, skeletonId) {
	console.log('Building skeleton:', usdSkeleton);

	const bones = [];
	const boneMap = new Map();
	let jointId = 0;

	/**
	 * Recursively build bone hierarchy
	 * @param {Object} skelNode - USD SkelNode
	 * @param {THREE.Bone} parentBone - Parent bone (null for root)
	 */
	function buildBoneHierarchy(skelNode, parentBone) {
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
		jointId++;

		// Apply rest transform if available
		if (skelNode.rest_transform) {
			const matrix = new THREE.Matrix4();
			const m = skelNode.rest_transform;
			// rest_transform is a flat array of 16 elements
			// Use fromArray() which expects column-major order (same as USD convention)
			if (Array.isArray(m) && m.length === 16) {
				matrix.fromArray(m);
			} else if (m[0] !== undefined && Array.isArray(m[0])) {
				// Legacy 2D array format (4x4) - flatten to column-major
				const flat = [
					m[0][0], m[1][0], m[2][0], m[3][0],
					m[0][1], m[1][1], m[2][1], m[3][1],
					m[0][2], m[1][2], m[2][2], m[3][2],
					m[0][3], m[1][3], m[2][3], m[3][3]
				];
				matrix.fromArray(flat);
			}
			matrix.decompose(bone.position, bone.quaternion, bone.scale);
		}

		if (parentBone) {
			parentBone.add(bone);
		} else {
			bones.push(bone); // Root bone
		}

		// Process children
		if (skelNode.children && skelNode.children.length > 0) {
			for (const childNode of skelNode.children) {
				buildBoneHierarchy(childNode, bone);
			}
		}

		return bone;
	}

	// Build from root node
	if (usdSkeleton.root_node) {
		const rootBone = buildBoneHierarchy(usdSkeleton.root_node, null);

		// Collect all bones in depth-first order
		const allBones = [];
		rootBone.traverse((bone) => {
			if (bone.isBone) {
				allBones.push(bone);
			}
		});

		return { bones: allBones, boneMap, rootBone };
	}

	console.warn('No root_node found in skeleton');
	return { bones: [], boneMap: new Map(), rootBone: null };
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
	const geometry = new THREE.SphereGeometry(0.05, 16, 16);

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
	//const usd_filename = "./assets/skintest.usda";
	const usd_filename = "./assets/CesiumMan.usdz";

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
	let skinnedMeshUSDData = null; // Store USD mesh data for adding skinning attributes later
	let detectedZUpFromPath = false;
	for (let i = 0; i < numMeshes; i++) {
		const mesh = usd_scene.getMesh(i);
		const hasJointIndices = mesh.jointIndices && mesh.jointIndices.length > 0;
		const hasJointWeights = mesh.jointWeights && mesh.jointWeights.length > 0;
		if (hasJointIndices || hasJointWeights) {
			hasSkinnedMeshData = true;
			// Store the first mesh with skinning data for later use
			if (!skinnedMeshUSDData) {
				skinnedMeshUSDData = {
					jointIndices: mesh.jointIndices,
					jointWeights: mesh.jointWeights,
					elementSize: mesh.elementSize || 4,
					absPath: mesh.absPath
				};
			}
			console.log(`Mesh ${i}: ${mesh.absPath}`);
			console.log(`  - skel_id: ${mesh.skel_id}`);
			console.log(`  - jointIndices: ${mesh.jointIndices ? mesh.jointIndices.length : 0} elements`);
			console.log(`  - jointWeights: ${mesh.jointWeights ? mesh.jointWeights.length : 0} elements`);
			console.log(`  - elementSize (influences per vertex): ${mesh.elementSize}`);
		}
		// Check path for Z_UP hint (some models have this in their hierarchy)
		if (mesh.absPath && (mesh.absPath.includes('Z_UP') || mesh.absPath.includes('z_up') || mesh.absPath.includes('Z_up'))) {
			detectedZUpFromPath = true;
		}
	}

	// Override upAxis if detected from path but metadata says Y
	if (detectedZUpFromPath && fileUpAxis === "Y") {
		console.log(`WARNING: Path contains 'Z_UP' but metadata says upAxis="Y". Overriding to "Z".`);
		fileUpAxis = "Z";
	}

	currentFileUpAxis = fileUpAxis; // Store globally for toggle function
	console.log(`upAxis (final): "${fileUpAxis}"`);
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
				// Create Three.js skeleton
				skeleton = new THREE.Skeleton(bones);

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
				child.bind(skeleton);
				child.castShadow = true;
				child.receiveShadow = true;

				skinnedMesh = child;
				characterGroup.add(skinnedMesh);
				foundSkinnedMesh = true;

				// Save original material
				originalMaterial = skinnedMesh.material;

				// Create skeleton helper for visualization
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
			} else {
				// No skeleton data - just add as regular mesh
				console.log('No skeleton data available, adding mesh without skeleton');
				child.castShadow = true;
				child.receiveShadow = true;
				characterGroup.add(child);

				// Save as skinnedMesh reference even though it's not actually skinned
				skinnedMesh = child;
				originalMaterial = child.material;
				foundSkinnedMesh = true;
			}
		}
	});

	if (!foundSkinnedMesh) {
		// If no skinned mesh found, try to find any mesh and add it
		console.log('No skinned mesh found');

		// Find first mesh
		let firstMesh = null;
		threeNode.traverse((child) => {
			if (child.isMesh && !firstMesh) {
				firstMesh = child;
			}
		});

		if (firstMesh && bones.length > 0 && rootBone) {
			// Have skeleton but mesh wasn't detected as skinned - create SkinnedMesh
			console.log('Creating SkinnedMesh from regular mesh with skeleton');
			skeleton = new THREE.Skeleton(bones);

			// Add skinning attributes to geometry from USD data if available
			const geometry = firstMesh.geometry;
			if (skinnedMeshUSDData && geometry.attributes.position) {
				const vertexCount = geometry.attributes.position.count;
				const influencesPerVertex = skinnedMeshUSDData.elementSize || 4;
				const usdVertexCount = Math.floor(skinnedMeshUSDData.jointIndices.length / influencesPerVertex);

				console.log(`Adding skinning attributes: ${vertexCount} Three.js vertices`);
				console.log(`  USD jointIndices: ${skinnedMeshUSDData.jointIndices.length} elements`);
				console.log(`  USD jointWeights: ${skinnedMeshUSDData.jointWeights.length} elements`);
				console.log(`  USD vertex count (inferred): ${usdVertexCount} (${influencesPerVertex} influences per vertex)`);

				// Check if vertex counts match
				if (vertexCount !== usdVertexCount) {
					console.warn(`Vertex count mismatch: Three.js has ${vertexCount}, USD has ${usdVertexCount}`);
					console.warn('Skinning may not work correctly - using min of both counts');
				}

				const effectiveVertexCount = Math.min(vertexCount, usdVertexCount);

				// Three.js always uses 4 influences per vertex
				const skinIndices = new Uint16Array(vertexCount * 4);
				const skinWeights = new Float32Array(vertexCount * 4);

				const usdJointIndices = skinnedMeshUSDData.jointIndices;
				const usdJointWeights = skinnedMeshUSDData.jointWeights;

				// Copy from USD format (influencesPerVertex) to Three.js format (4)
				for (let i = 0; i < effectiveVertexCount; i++) {
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

				// For any remaining vertices (if Three.js has more), set default values
				for (let i = effectiveVertexCount; i < vertexCount; i++) {
					for (let j = 0; j < 4; j++) {
						const dstIdx = i * 4 + j;
						skinIndices[dstIdx] = 0;
						skinWeights[dstIdx] = j === 0 ? 1 : 0; // First bone gets full weight
					}
				}

				// Add attributes to geometry
				geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(skinIndices, 4));
				geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(skinWeights, 4));
				console.log('Added skinIndex and skinWeight attributes to geometry');
			} else {
				console.warn('No USD skinning data available to add to geometry');
			}

			// Validate rootBone before creating SkinnedMesh
			if (!rootBone || !(rootBone instanceof THREE.Bone)) {
				console.error('Invalid rootBone:', rootBone);
				throw new Error('rootBone is not a valid THREE.Bone');
			}

			console.log(`Creating SkinnedMesh with ${bones.length} bones, rootBone: ${rootBone.name}`);

			const newSkinnedMesh = new THREE.SkinnedMesh(firstMesh.geometry, firstMesh.material);

			// Add root bone to mesh first
			newSkinnedMesh.add(rootBone);

			// Update bone world matrices after adding to scene graph
			newSkinnedMesh.updateMatrixWorld(true);

			// Now bind skeleton - this computes inverse bind matrices
			newSkinnedMesh.bind(skeleton);
			newSkinnedMesh.castShadow = true;
			newSkinnedMesh.receiveShadow = true;

			skinnedMesh = newSkinnedMesh;
			characterGroup.add(skinnedMesh);

			// Save original material
			originalMaterial = skinnedMesh.material;

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
		} else {
			// No skeleton or no mesh - just add the scene as-is
			console.log('Adding scene without skeleton');
			if (firstMesh) {
				firstMesh.castShadow = true;
				firstMesh.receiveShadow = true;
				characterGroup.add(firstMesh);
				skinnedMesh = firstMesh;
				originalMaterial = firstMesh.material;
			} else {
				characterGroup.add(threeNode);
			}

			// Update joint hierarchy display (empty)
			if (window.updateJointHierarchy) {
				window.updateJointHierarchy(generateJointHierarchy([]));
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

	// Apply Z-up to Y-up conversion if enabled AND the file is actually Z-up
	if (animationParams.applyUpAxisConversion && fileUpAxis === "Z") {
		usdSceneRoot.rotation.x = -Math.PI / 2;
		console.log(`[processUSDScene] Applied Z-up to Y-up conversion (file upAxis="${fileUpAxis}"): rotation.x =`, usdSceneRoot.rotation.x);
	} else if (animationParams.applyUpAxisConversion && fileUpAxis !== "Y") {
		console.warn(`[processUSDScene] File upAxis is "${fileUpAxis}" (not Y or Z), no rotation applied`);
	} else {
		// Reset rotation (either disabled or file is already Y-up)
		usdSceneRoot.rotation.x = 0;
		console.log(`[processUSDScene] No upAxis conversion needed (file upAxis="${fileUpAxis}", conversion ${animationParams.applyUpAxisConversion ? 'enabled' : 'disabled'})`);
	}

	// Update shadow camera frustum based on scene bounds
	usdSceneRoot.updateMatrixWorld(true);
	const sceneBounds = computeSceneBoundingBox(usdSceneRoot);
	updateShadowCameraFromBounds(directionalLight, sceneBounds);
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
	console.log('Please upload a USD file (with or without skeletal animation).');
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
	showSkeleton: true,
	toggleSkeleton: function() {
		if (skeletonHelper) {
			skeletonHelper.visible = this.showSkeleton;
		}
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
			if (!weightVisualizationMaterial) {
				weightVisualizationMaterial = createWeightVisualizationMaterial();
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
	applyUpAxisConversion: true,
	toggleUpAxisConversion: function() {
		if (this.applyUpAxisConversion && currentFileUpAxis === "Z") {
			// Apply Z-up to Y-up conversion (-90 degrees around X axis)
			usdSceneRoot.rotation.x = -Math.PI / 2;
			console.log(`[toggleUpAxisConversion] Applied Z-up to Y-up rotation (file upAxis="${currentFileUpAxis}"): usdSceneRoot.rotation.x =`, usdSceneRoot.rotation.x);
		} else {
			// Reset rotation (either disabled or file is already Y-up)
			usdSceneRoot.rotation.x = 0;
			if (this.applyUpAxisConversion && currentFileUpAxis !== "Z") {
				console.log(`[toggleUpAxisConversion] No rotation needed (file upAxis="${currentFileUpAxis}"): usdSceneRoot.rotation.x =`, usdSceneRoot.rotation.x);
			} else {
				console.log(`[toggleUpAxisConversion] Reset rotation (conversion disabled): usdSceneRoot.rotation.x =`, usdSceneRoot.rotation.x);
			}
		}
	}
};

// GUI setup
const gui = new GUI();
gui.title('Skeletal Animation Controls');

// Playback controls
const playbackFolder = gui.addFolder('Playback');
playbackFolder.add(animationParams, 'playPause').name('Play / Pause');
playbackFolder.add(animationParams, 'reset').name('Reset');
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
visualFolder.add(animationParams, 'showSkeleton')
	.name('Show Skeleton')
	.onChange(() => animationParams.toggleSkeleton());
visualFolder.add(animationParams, 'enableShadows')
	.name('Enable Shadows')
	.onChange(() => animationParams.toggleShadows());
visualFolder.add(animationParams, 'applyUpAxisConversion')
	.name('Z-up to Y-up')
	.onChange(() => animationParams.toggleUpAxisConversion());
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
const boneReductionFolder = gui.addFolder('Bone Reduction (Next Load)');
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
boneReductionFolder.close(); // Closed by default as advanced feature

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
	if (selectedJoint && transformControls.visible) {
		transformControls.updateMatrixWorld();
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
