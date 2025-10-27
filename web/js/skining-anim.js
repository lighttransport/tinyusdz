import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';

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

// Lighting
const ambientLight = new THREE.AmbientLight(0x404040, 1);
scene.add(ambientLight);

const directionalLight = new THREE.DirectionalLight(0xffffff, 1);
directionalLight.position.set(5, 10, 5);
directionalLight.castShadow = true;
directionalLight.shadow.camera.left = -10;
directionalLight.shadow.camera.right = 10;
directionalLight.shadow.camera.top = 10;
directionalLight.shadow.camera.bottom = -10;
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

// Character root group
const characterGroup = new THREE.Group();
scene.add(characterGroup);

// Animation state
let skinnedMesh = null;
let skeleton = null;
let mixer = null;
let animationAction = null;
let usdAnimations = [];
let boneMap = new Map(); // Map from joint_id to THREE.Bone

// ===========================================
// USD Skeletal Animation Extraction Functions
// ===========================================

/**
 * Convert USD skeletal animation data to Three.js AnimationClip
 * Extracts only SkeletonJoint animations from USD SkelAnimation
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {Map} boneMap - Map from joint_id to THREE.Bone
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
function convertUSDSkeletalAnimationsToThreeJS(usdLoader, boneMap) {
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
		bone.name = skelNode.joint_name || skelNode.joint_path || `joint_${jointId}`;

		// Store mapping from joint_id to bone
		const currentJointId = skelNode.joint_id !== undefined ? skelNode.joint_id : jointId;
		boneMap.set(currentJointId, bone);
		jointId++;

		// Apply rest transform if available
		if (skelNode.rest_transform) {
			const matrix = new THREE.Matrix4();
			const m = skelNode.rest_transform;
			// USD uses row-major, Three.js uses column-major
			matrix.set(
				m[0][0], m[1][0], m[2][0], m[3][0],
				m[0][1], m[1][1], m[2][1], m[3][1],
				m[0][2], m[1][2], m[2][2], m[3][2],
				m[0][3], m[1][3], m[2][3], m[3][3]
			);
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
 * Load USD file and extract skeletal mesh and animations
 * @param {ArrayBuffer} arrayBuffer - USD file data
 * @param {string} filename - File name
 */
async function loadUSDFromArrayBuffer(arrayBuffer, filename) {
	// Clear existing model
	if (skinnedMesh) {
		characterGroup.remove(skinnedMesh);
		skinnedMesh = null;
	}
	if (skeletonHelper) {
		scene.remove(skeletonHelper);
		skeletonHelper = null;
	}

	// Reset animations
	usdAnimations = [];
	boneMap.clear();
	animationParams.hasUSDAnimations = false;
	animationParams.usdAnimationCount = 0;

	const loader = new TinyUSDZLoader();
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });

	// Convert ArrayBuffer to Uint8Array
	const uint8Array = new Uint8Array(arrayBuffer);

	// Load USD scene from binary data
	const usd_scene = await loader.loadFromBinary(uint8Array, filename);

	console.log('USD scene loaded:', usd_scene);

	// Get skeletons
	const numSkeletons = usd_scene.numSkeletons ? usd_scene.numSkeletons() : 0;
	console.log(`Found ${numSkeletons} skeletons in USD file`);

	if (numSkeletons === 0) {
		console.warn('No skeletons found in USD file');
		alert('No skeletons found in this USD file. Please load a file with skeletal animation (SkelAnimation).');
		return;
	}

	// Get first skeleton (for simplicity, only support one skeleton for now)
	const usdSkeleton = usd_scene.getSkeleton(0);
	console.log('USD Skeleton:', usdSkeleton);

	// Build Three.js skeleton
	const { bones, boneMap: newBoneMap, rootBone } = buildSkeletonFromUSD(usdSkeleton, 0);
	boneMap = newBoneMap;

	console.log(`Built skeleton with ${bones.length} bones`);

	// Update skeleton info display
	if (window.updateSkeletonInfo) {
		window.updateSkeletonInfo(numSkeletons, bones.length);
	}

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
	const threeNode = TinyUSDZLoaderUtils.buildThreeNode(usdRootNode, defaultMtl, usd_scene, options);

	// Find skinned meshes in the loaded geometry
	let foundSkinnedMesh = false;
	threeNode.traverse((child) => {
		if (child.isMesh && child.geometry.attributes.skinIndex && child.geometry.attributes.skinWeight) {
			console.log('Found skinned mesh:', child.name);

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

			// Create skeleton helper for visualization
			skeletonHelper = new THREE.SkeletonHelper(skinnedMesh);
			skeletonHelper.visible = animationParams.showSkeleton;
			scene.add(skeletonHelper);
		}
	});

	if (!foundSkinnedMesh) {
		// If no skinned mesh found, just add the geometry and attach skeleton
		console.log('No skinned mesh found, creating basic SkinnedMesh');

		// Find first mesh
		let firstMesh = null;
		threeNode.traverse((child) => {
			if (child.isMesh && !firstMesh) {
				firstMesh = child;
			}
		});

		if (firstMesh) {
			skeleton = new THREE.Skeleton(bones);
			const newSkinnedMesh = new THREE.SkinnedMesh(firstMesh.geometry, firstMesh.material);
			newSkinnedMesh.add(rootBone);
			newSkinnedMesh.bind(skeleton);
			newSkinnedMesh.castShadow = true;
			newSkinnedMesh.receiveShadow = true;

			skinnedMesh = newSkinnedMesh;
			characterGroup.add(skinnedMesh);

			skeletonHelper = new THREE.SkeletonHelper(skinnedMesh);
			skeletonHelper.visible = animationParams.showSkeleton;
			scene.add(skeletonHelper);
		} else {
			// Fallback: just add the geometry
			characterGroup.add(threeNode);
		}
	}

	// Extract skeletal animations if available
	try {
		const animationInfos = usd_scene.getAllAnimationInfos();
		usdAnimations = convertUSDSkeletalAnimationsToThreeJS(usd_scene, boneMap);

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
			if (skinnedMesh && usdAnimations.length > 0) {
				mixer = new THREE.AnimationMixer(skinnedMesh);
				playAnimation(0);
			}
		} else {
			if (window.updateAnimationList) {
				window.updateAnimationList([], []);
			}
			console.log('No skeletal animations found in USD file');
		}
	} catch (error) {
		console.error('Error extracting skeletal animations:', error);
		if (window.updateAnimationList) {
			window.updateAnimationList([], []);
		}
	}
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
		alert('Failed to load USD file: ' + error.message);
	}
});

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
	speed: 1.0,
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
	}
};

// GUI setup
const gui = new GUI();
gui.title('Skeletal Animation Controls');

// Playback controls
const playbackFolder = gui.addFolder('Playback');
playbackFolder.add(animationParams, 'playPause').name('Play / Pause');
playbackFolder.add(animationParams, 'reset').name('Reset');
playbackFolder.add(animationParams, 'speed', 0, 3, 0.1).name('Speed').onChange(() => {
	if (animationAction) {
		animationAction.setEffectiveTimeScale(animationParams.speed);
	}
});
playbackFolder.add(animationParams, 'time', 0, 30, 0.01)
	.name('Timeline').listen().onChange(() => {
		if (animationAction) {
			animationAction.time = animationParams.time;
		}
	});
playbackFolder.open();

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
visualFolder.open();

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
	if (mixer && animationParams.isPlaying) {
		mixer.update(deltaTime * animationParams.speed);

		// Update time display
		if (animationAction) {
			animationParams.time = animationAction.time;
		}
	}

	// Update skeleton helper
	if (skeletonHelper) {
		skeletonHelper.update();
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

console.log('Skeletal animation demo initialized. Load a USD file with skeletal animations to begin.');
