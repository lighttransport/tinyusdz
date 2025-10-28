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

// Animated objects - Create a simple hierarchy
const parentGroup = new THREE.Group();
scene.add(parentGroup);

// Parent object (will be loaded from USD and animated with KeyframeTracks)
let parentCube = null;
let usdAnimations = []; // Store USD animations from the file

// ===========================================
// USD Animation Extraction Functions
// ===========================================

/**
 * Convert USD animation data to Three.js AnimationClip
 * Supports both channel/sampler and track-based animation structures
 * @param {Object} usdLoader - TinyUSDZ loader instance
 * @param {THREE.Object3D} sceneRoot - Three.js scene containing the loaded geometry
 * @returns {Array<THREE.AnimationClip>} Array of Three.js AnimationClips
 */
function convertUSDAnimationsToThreeJS(usdLoader, sceneRoot) {
	const animationClips = [];

	// Get number of animations
	const numAnimations = usdLoader.numAnimations();
	console.log(`Found ${numAnimations} animations in USD file`);

	// Get summary of all animations
	const animationInfos = usdLoader.getAllAnimationInfos();
	console.log('Animation summaries:', animationInfos);

	// Build node index map for faster lookup
	const nodeIndexMap = new Map();
	let nodeIndex = 0;
	sceneRoot.traverse((obj) => {
		nodeIndexMap.set(nodeIndex++, obj);
	});

	// Convert each animation to Three.js format
	for (let i = 0; i < numAnimations; i++) {
		const usdAnimation = usdLoader.getAnimation(i);
		console.log(`Processing animation ${i}: ${usdAnimation.name}`);

		// Check if this is a track-based animation (legacy format)
		if (usdAnimation.tracks && usdAnimation.tracks.length > 0) {
			console.log(`Animation ${i} uses track-based format with ${usdAnimation.tracks.length} tracks`);

			// Process track-based animation
			const keyframeTracks = [];

			// Find the target object - for track animations, usually the first child after scene root
			let targetObject = sceneRoot;
			// Try to find the animated object by name from the animation
			if (usdAnimation.name && usdAnimation.name.includes('_')) {
				const targetName = usdAnimation.name.split('_')[0]; // e.g., "AnimatedCube" from "AnimatedCube_xform"
				sceneRoot.traverse((obj) => {
					if (obj.name && obj.name.includes(targetName)) {
						targetObject = obj;
					}
				});
			}

			// If we can't find it by name, use the first mesh or group
			if (targetObject === sceneRoot) {
				sceneRoot.traverse((obj) => {
					if ((obj.isMesh || obj.isGroup) && obj !== sceneRoot) {
						targetObject = obj;
						return; // Stop traversal once we find the first mesh/group
					}
				});
			}

			const targetName = targetObject.name || 'AnimatedObject';
			console.log(`Target object for animation: ${targetName}`);

			// Process each track
			for (const track of usdAnimation.tracks) {
				if (!track.times || !track.values) {
					console.warn('Track missing times or values');
					continue;
				}

				// Convert times and values to arrays
				const times = Array.isArray(track.times) ? track.times : Array.from(track.times);
				const values = Array.isArray(track.values) ? track.values : Array.from(track.values);
				const interpolation = getUSDInterpolationMode(track.interpolation);

				console.log(`Processing track: ${track.path}, ${times.length} keyframes`);

				// Create appropriate Three.js KeyframeTrack based on path
				let keyframeTrack;

				switch (track.path) {
					case 'translation':
					case 'Translation':
						keyframeTrack = new THREE.VectorKeyframeTrack(
							`${targetName}.position`,
							times,
							values,
							interpolation
						);
						break;

					case 'rotation':
					case 'Rotation':
						// Rotation is stored as quaternions (x, y, z, w)
						keyframeTrack = new THREE.QuaternionKeyframeTrack(
							`${targetName}.quaternion`,
							times,
							values,
							interpolation
						);
						break;

					case 'scale':
					case 'Scale':
						keyframeTrack = new THREE.VectorKeyframeTrack(
							`${targetName}.scale`,
							times,
							values,
							interpolation
						);
						break;

					default:
						console.warn(`Unknown track path: ${track.path}`);
						continue;
				}

				if (keyframeTrack) {
					keyframeTracks.push(keyframeTrack);
				}
			}

			// Create Three.js AnimationClip from tracks
			if (keyframeTracks.length > 0) {
				const clip = new THREE.AnimationClip(
					usdAnimation.name || `Animation_${i}`,
					usdAnimation.duration || -1, // -1 will auto-calculate from tracks
					keyframeTracks
				);

				animationClips.push(clip);
				console.log(`Created clip: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}`);
			}

			continue; // Skip to next animation
		}

		// Handle channel-based animation (newer format)
		if (!usdAnimation.channels || !usdAnimation.samplers) {
			console.warn(`Animation ${i} missing channels/samplers and tracks`);
			continue;
		}

		// Filter for node animations only (skip skeletal animations)
		const nodeChannels = usdAnimation.channels.filter(channel => {
			const targetType = channel.target_type || 'SceneNode'; // Default to SceneNode for backward compat
			return targetType === 'SceneNode';
		});

		if (nodeChannels.length === 0) {
			console.log(`Animation ${i} has no SceneNode channels (skipping skeletal-only animation)`);
			continue;
		}

		console.log(`Animation ${i}: ${nodeChannels.length} node channels (${usdAnimation.channels.length - nodeChannels.length} skeletal channels skipped)`);

		// Create Three.js KeyframeTracks from USD animation channels
		const keyframeTracks = [];

		for (const channel of nodeChannels) {
			// Get sampler data
			const sampler = usdAnimation.samplers[channel.sampler];
			if (!sampler || !sampler.times || !sampler.values) {
				console.warn(`Invalid sampler for channel`);
				continue;
			}

			// Find the Three.js object for this channel
			const targetObject = nodeIndexMap.get(channel.target_node);
			if (!targetObject) {
				console.warn(`Could not find object at node index: ${channel.target_node}`);
				continue;
			}

			// Convert times and values to arrays
			const times = Array.isArray(sampler.times) ? sampler.times : Array.from(sampler.times);
			const values = Array.isArray(sampler.values) ? sampler.values : Array.from(sampler.values);

			// Create appropriate Three.js KeyframeTrack based on path
			let keyframeTrack;
			const targetName = targetObject.name || `node_${channel.target_node}`;
			const interpolation = getUSDInterpolationMode(sampler.interpolation);

			switch (channel.path) {
				case 'Translation':
					keyframeTrack = new THREE.VectorKeyframeTrack(
						`${targetName}.position`,
						times,
						values,
						interpolation
					);
					break;

				case 'Rotation':
					// Rotation is stored as quaternions (x, y, z, w)
					keyframeTrack = new THREE.QuaternionKeyframeTrack(
						`${targetName}.quaternion`,
						times,
						values,
						interpolation
					);
					break;

				case 'Scale':
					keyframeTrack = new THREE.VectorKeyframeTrack(
						`${targetName}.scale`,
						times,
						values,
						interpolation
					);
					break;

				case 'Weights':
					// For morph targets
					keyframeTrack = new THREE.NumberKeyframeTrack(
						`${targetName}.morphTargetInfluences`,
						times,
						values,
						interpolation
					);
					break;

				default:
					console.warn(`Unknown animation path: ${channel.path}`);
					continue;
			}

			if (keyframeTrack) {
				keyframeTracks.push(keyframeTrack);
			}
		}

		// Create Three.js AnimationClip
		if (keyframeTracks.length > 0) {
			const clip = new THREE.AnimationClip(
				usdAnimation.name || `Animation_${i}`,
				usdAnimation.duration || -1, // -1 will auto-calculate from tracks
				keyframeTracks
			);

			animationClips.push(clip);
			console.log(`Created clip: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}`);
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

// Load USD model asynchronously
async function loadUSDModel() {
	const loader = new TinyUSDZLoader();

	// Initialize the loader (wait for WASM module to load)
	// Use memory64: false for browser compatibility
	// Use useZstdCompressedWasm: false since compressed WASM is not available
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });

	//const usd_filename = "./assets/cube-animation.usda";
	//const usd_filename = "./assets/suzanne-xform.usdc";
	const usd_filename = "./assets/wings-3.usdc";

	// Load USD scene
	const usd_scene = await loader.loadAsync(usd_filename);

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

	// Setup the loaded model
	threeNode.name = 'parentCube'; // Keep the same name for animation tracking
	threeNode.castShadow = true;
	threeNode.receiveShadow = true;
	threeNode.position.y = 0.5;

	// Traverse and enable shadows for all meshes
	threeNode.traverse((child) => {
		if (child.isMesh) {
			child.castShadow = true;
			child.receiveShadow = true;
		}
	});

	parentCube = threeNode;
	parentGroup.add(parentCube);

	// Extract USD animations if available
	try {
		const animationInfos = usd_scene.getAllAnimationInfos();
		usdAnimations = convertUSDAnimationsToThreeJS(usd_scene, parentGroup);

		if (usdAnimations.length > 0) {
			console.log(`Extracted ${usdAnimations.length} animations from USD file`);

			// Update animation parameters
			animationParams.hasUSDAnimations = true;
			animationParams.usdAnimationCount = usdAnimations.length;
			animationParams.useUSDAnimation = true; // Auto-enable USD animations

			// Show the USD animation folder in GUI
			if (window.usdAnimationFolder) {
				window.usdAnimationFolder.show();
			}

			// Update animation list in UI with type information
			if (window.updateAnimationList) {
				window.updateAnimationList(usdAnimations, animationInfos);
			}

			// Log animation details
			usdAnimations.forEach((clip, index) => {
				const info = animationInfos[index];
				let typeStr = '';
				if (info) {
					const types = [];
					if (info.has_skeletal_animation) types.push('skeletal');
					if (info.has_node_animation) types.push('node');
					if (types.length > 0) typeStr = ` [${types.join('+')}]`;
				}
				console.log(`Animation ${index}: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}${typeStr}`);
			});

			// Set time range from first USD animation
			const firstClip = usdAnimations[0];
			if (firstClip && firstClip.duration > 0) {
				animationParams.beginTime = 0;
				animationParams.endTime = firstClip.duration;
				animationParams.duration = firstClip.duration;
				animationParams.time = 0; // Reset time to beginning
				console.log(`Set time range from USD animation: 0s - ${firstClip.duration}s`);

				// Update GUI controllers if they exist
				updateTimeRangeGUIControllers(firstClip.duration);
			}

			// Play the first USD animation automatically
			playUSDAnimation(0);
		} else {
			// No USD animations found - use synthetic animations
			console.log('No USD animations found, using synthetic animations');
			animationParams.useUSDAnimation = false;
			updateAnimationClip();
		}
	} catch (error) {
		console.log('No animations found in USD file or animation extraction not supported:', error);
		// Fallback to synthetic animations
		animationParams.useUSDAnimation = false;
		updateAnimationClip();
	}
}

// Play USD animation by index
function playUSDAnimation(index) {
	if (index >= 0 && index < usdAnimations.length) {
		// Ensure mixer exists
		if (!mixer && parentCube) {
			mixer = new THREE.AnimationMixer(parentCube);
		}

		// Stop current animation
		if (animationAction) {
			animationAction.stop();
		}

		// Play USD animation
		const clip = usdAnimations[index];
		if (mixer && clip) {
			animationAction = mixer.clipAction(clip);
			animationAction.loop = THREE.LoopRepeat;
			animationAction.play();
			console.log(`Playing USD animation: ${clip.name}`);
		}
	}
}

// Create synthetic KeyframeTracks for the cube
function createKeyframeTracks(duration, numKeyframes = 20) {
	const times = [];
	const positionValues = [];
	const rotationValues = [];
	const scaleValues = [];

	for (let i = 0; i < numKeyframes; i++) {
		const t = (i / (numKeyframes - 1)) * duration;
		times.push(t);

		// Position track - figure-8 motion
		const angle = (i / (numKeyframes - 1)) * Math.PI * 2;
		positionValues.push(
			Math.sin(angle) * 2,           // x
			0.5 + Math.sin(angle * 2) * 0.5, // y
			Math.cos(angle) * 2            // z
		);

		// Rotation track - spinning on all axes
		const quat = new THREE.Quaternion();
		quat.setFromEuler(new THREE.Euler(
			angle * 2,       // x rotation
			angle * 3,       // y rotation
			angle * 1.5      // z rotation
		));
		rotationValues.push(quat.x, quat.y, quat.z, quat.w);

		// Scale track - pulsing effect
		const scale = 1.0 + Math.sin(angle * 4) * 0.3;
		scaleValues.push(scale, scale, scale);
	}

	const positionTrack = new THREE.VectorKeyframeTrack(
		'parentCube.position',
		times,
		positionValues
	);

	const rotationTrack = new THREE.QuaternionKeyframeTrack(
		'parentCube.quaternion',
		times,
		rotationValues
	);

	const scaleTrack = new THREE.VectorKeyframeTrack(
		'parentCube.scale',
		times,
		scaleValues
	);

	return { positionTrack, rotationTrack, scaleTrack };
}

// Create animation mixer for the cube
let mixer = null;
let animationAction = null;
let currentTracks = createKeyframeTracks(10);
let enabledTracks = {
	position: true,
	rotation: true,
	scale: true
};

function updateAnimationClip() {
	// Only create animation if parentCube is loaded
	if (!parentCube) {
		return;
	}

	// Create mixer if not already created
	if (!mixer) {
		mixer = new THREE.AnimationMixer(parentCube);
	}

	// Stop and remove current action
	if (animationAction) {
		animationAction.stop();
		mixer.uncacheAction(animationAction.getClip());
	}

	// Create new tracks based on enabled state
	const tracks = [];
	const newTracks = createKeyframeTracks(animationParams.duration);

	if (enabledTracks.position) {
		tracks.push(newTracks.positionTrack);
	}
	if (enabledTracks.rotation) {
		tracks.push(newTracks.rotationTrack);
	}
	if (enabledTracks.scale) {
		tracks.push(newTracks.scaleTrack);
	}

	// Create new clip and action
	if (tracks.length > 0) {
		const clip = new THREE.AnimationClip('cubeAnimation', animationParams.duration, tracks);
		animationAction = mixer.clipAction(clip);
		animationAction.loop = THREE.LoopRepeat;
		animationAction.clampWhenFinished = false;
		animationAction.play();
	}

	currentTracks = newTracks;
}

// Child sphere
const childGroup = new THREE.Group();
childGroup.position.set(2, 0, 0);
parentGroup.add(childGroup);

const sphereGeometry = new THREE.SphereGeometry(0.4, 32, 32);
const sphereMaterial = new THREE.MeshStandardMaterial({
	color: 0x6bb6ff,
	roughness: 0.3,
	metalness: 0.7
});
const childSphere = new THREE.Mesh(sphereGeometry, sphereMaterial);
childSphere.castShadow = true;
childSphere.receiveShadow = true;
childSphere.position.y = 0.5;
childGroup.add(childSphere);

// Grandchild cone
const grandchildGroup = new THREE.Group();
grandchildGroup.position.set(1.5, 0, 0);
childGroup.add(grandchildGroup);

const coneGeometry = new THREE.ConeGeometry(0.3, 0.8, 32);
const coneMaterial = new THREE.MeshStandardMaterial({
	color: 0x95e06c,
	roughness: 0.4,
	metalness: 0.5
});
const grandchildCone = new THREE.Mesh(coneGeometry, coneMaterial);
grandchildCone.castShadow = true;
grandchildCone.receiveShadow = true;
grandchildCone.position.y = 0.9;
grandchildGroup.add(grandchildCone);

// Animation parameters
const animationParams = {
	isPlaying: true,
	playPause: function() {
		this.isPlaying = !this.isPlaying;
		if (animationAction) {
			if (this.isPlaying) {
				animationAction.paused = false;
			} else {
				animationAction.paused = true;
			}
		}
	},
	reset: function() {
		animationParams.time = animationParams.beginTime;
		animationParams.speed = 1.0;
		if (animationAction) {
			animationAction.time = animationParams.beginTime;
		}
	},
	time: 0,
	beginTime: 0,
	endTime: 10,
	duration: 10, // seconds
	speed: 1.0,

	// Animation toggles
	parentRotation: true,
	childRotation: true,
	grandchildRotation: true,
	parentBounce: true,
	childOrbit: true,

	// KeyframeTrack toggles for cube
	cubePosition: true,
	cubeRotation: true,
	cubeScale: true,

	// USD Animation properties
	hasUSDAnimations: false,
	usdAnimationCount: 0,
	currentUSDAnimation: 0,
	useUSDAnimation: false,
	playUSDAnimation: function() {
		if (usdAnimations.length > 0) {
			this.useUSDAnimation = true;
			playUSDAnimation(this.currentUSDAnimation);
		}
	},
	playSyntheticAnimation: function() {
		this.useUSDAnimation = false;
		updateAnimationClip();
	},
	selectUSDAnimation: function() {
		if (this.useUSDAnimation && usdAnimations.length > this.currentUSDAnimation) {
			playUSDAnimation(this.currentUSDAnimation);
		}
	},

	// Update functions
	updateDuration: function() {
		this.duration = this.endTime - this.beginTime;
		updateAnimationClip();
	},
	toggleCubePosition: function() {
		enabledTracks.position = this.cubePosition;
		updateAnimationClip();
	},
	toggleCubeRotation: function() {
		enabledTracks.rotation = this.cubeRotation;
		updateAnimationClip();
	},
	toggleCubeScale: function() {
		enabledTracks.scale = this.cubeScale;
		updateAnimationClip();
	}
};

// GUI setup
const gui = new GUI();
gui.title('Animation Controls');

// Store references to GUI controllers for dynamic updates
let timelineController = null;
let beginTimeController = null;
let endTimeController = null;

// Playback controls
const playbackFolder = gui.addFolder('Playback');
playbackFolder.add(animationParams, 'playPause').name('Play / Pause');
playbackFolder.add(animationParams, 'reset').name('Reset');
playbackFolder.add(animationParams, 'speed', 0, 3, 0.1).name('Speed');
timelineController = playbackFolder.add(animationParams, 'time', 0, 30, 0.01)
	.name('Timeline')
	.listen()
	.onChange((value) => {
		// When user manually scrubs the timeline, update animation action
		if (animationAction) {
			animationAction.time = value;
		}
	});
playbackFolder.open();

// USD Animation controls (will be populated when USD file is loaded)
const usdAnimationFolder = gui.addFolder('USD Animations');
window.usdAnimationFolder = usdAnimationFolder; // Make it accessible globally
usdAnimationFolder.add(animationParams, 'hasUSDAnimations').name('Has USD Animations').listen().disable();
usdAnimationFolder.add(animationParams, 'usdAnimationCount').name('Animation Count').listen().disable();
usdAnimationFolder.add(animationParams, 'currentUSDAnimation', 0, 10, 1)
	.name('Select Animation')
	.onChange(() => animationParams.selectUSDAnimation());
usdAnimationFolder.add(animationParams, 'playUSDAnimation').name('Play USD Animation');
usdAnimationFolder.add(animationParams, 'playSyntheticAnimation').name('Play Synthetic Animation');
// Hide the folder initially, will show when USD animations are loaded
usdAnimationFolder.hide();

// Time range controls
const timeRangeFolder = gui.addFolder('Time Range');
beginTimeController = timeRangeFolder.add(animationParams, 'beginTime', 0, 29, 0.1)
	.name('Begin Time (s)')
	.onChange(() => {
		if (animationParams.beginTime >= animationParams.endTime) {
			animationParams.beginTime = animationParams.endTime - 0.1;
		}
		animationParams.updateDuration();
	});
endTimeController = timeRangeFolder.add(animationParams, 'endTime', 0.1, 30, 0.1)
	.name('End Time (s)')
	.onChange(() => {
		if (animationParams.endTime <= animationParams.beginTime) {
			animationParams.endTime = animationParams.beginTime + 0.1;
		}
		animationParams.updateDuration();
	});
timeRangeFolder.add(animationParams, 'duration', 0.1, 30, 0.1)
	.name('Duration (s)')
	.listen()
	.disable();
timeRangeFolder.open();

// Function to update time range GUI controllers when animation is loaded
function updateTimeRangeGUIControllers(maxDuration) {
	const newMax = Math.max(maxDuration, 30); // Ensure minimum of 30s for usability

	// Update timeline controller
	if (timelineController) {
		timelineController.max(newMax);
		timelineController.updateDisplay();
	}

	// Update begin time controller
	if (beginTimeController) {
		beginTimeController.max(newMax - 0.1);
		beginTimeController.updateDisplay();
	}

	// Update end time controller
	if (endTimeController) {
		endTimeController.max(newMax);
		endTimeController.updateDisplay();
	}

	console.log(`Updated GUI time range to 0-${newMax}s`);
}

// Cube KeyframeTrack controls
const cubeTracksFolder = gui.addFolder('Cube KeyframeTracks');
cubeTracksFolder.add(animationParams, 'cubePosition')
	.name('Position Track')
	.onChange(() => animationParams.toggleCubePosition());
cubeTracksFolder.add(animationParams, 'cubeRotation')
	.name('Rotation Track')
	.onChange(() => animationParams.toggleCubeRotation());
cubeTracksFolder.add(animationParams, 'cubeScale')
	.name('Scale Track')
	.onChange(() => animationParams.toggleCubeScale());
cubeTracksFolder.open();

// Animation toggles for other objects
const togglesFolder = gui.addFolder('Other Object Animations');
togglesFolder.add(animationParams, 'parentRotation').name('Parent Rotation');
togglesFolder.add(animationParams, 'childRotation').name('Child Rotation');
togglesFolder.add(animationParams, 'grandchildRotation').name('Grandchild Rotation');
togglesFolder.add(animationParams, 'parentBounce').name('Parent Bounce');
togglesFolder.add(animationParams, 'childOrbit').name('Child Orbit');

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

// Function to load a USD file from ArrayBuffer
async function loadUSDFromArrayBuffer(arrayBuffer, filename) {
	// Clear existing model
	if (parentCube) {
		parentGroup.remove(parentCube);
		parentCube = null;
	}

	// Reset animations
	usdAnimations = [];
	animationParams.hasUSDAnimations = false;
	animationParams.usdAnimationCount = 0;

	const loader = new TinyUSDZLoader();
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });

	// Create a Blob URL from the ArrayBuffer
	// This allows the loader to load the file as if it were a normal URL
	const blob = new Blob([arrayBuffer]);
	const blobUrl = URL.createObjectURL(blob);

	console.log(`Loading USD from file: ${filename} (${(arrayBuffer.byteLength / 1024).toFixed(2)} KB)`);

	// Load USD scene from Blob URL
	const usd_scene = await loader.loadAsync(blobUrl);

	// Clean up the Blob URL after loading
	URL.revokeObjectURL(blobUrl);

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

	// Setup the loaded model
	threeNode.name = 'parentCube';
	threeNode.castShadow = true;
	threeNode.receiveShadow = true;
	threeNode.position.y = 0.5;

	// Traverse and enable shadows for all meshes
	threeNode.traverse((child) => {
		if (child.isMesh) {
			child.castShadow = true;
			child.receiveShadow = true;
		}
	});

	parentCube = threeNode;
	parentGroup.add(parentCube);

	// Extract USD animations if available
	try {
		const animationInfos = usd_scene.getAllAnimationInfos();
		usdAnimations = convertUSDAnimationsToThreeJS(usd_scene, parentGroup);

		if (usdAnimations.length > 0) {
			console.log(`Extracted ${usdAnimations.length} animations from USD file`);

			// Update animation parameters
			animationParams.hasUSDAnimations = true;
			animationParams.usdAnimationCount = usdAnimations.length;
			animationParams.useUSDAnimation = true; // Auto-enable USD animations

			// Show the USD animation folder in GUI
			if (window.usdAnimationFolder) {
				window.usdAnimationFolder.show();
			}

			// Update animation list in UI with type information
			if (window.updateAnimationList) {
				window.updateAnimationList(usdAnimations, animationInfos);
			}

			// Log animation details
			usdAnimations.forEach((clip, index) => {
				const info = animationInfos[index];
				let typeStr = '';
				if (info) {
					const types = [];
					if (info.has_skeletal_animation) types.push('skeletal');
					if (info.has_node_animation) types.push('node');
					if (types.length > 0) typeStr = ` [${types.join('+')}]`;
				}
				console.log(`Animation ${index}: ${clip.name}, duration: ${clip.duration}s, tracks: ${clip.tracks.length}${typeStr}`);
			});

			// Set time range from first USD animation
			const firstClip = usdAnimations[0];
			if (firstClip && firstClip.duration > 0) {
				animationParams.beginTime = 0;
				animationParams.endTime = firstClip.duration;
				animationParams.duration = firstClip.duration;
				animationParams.time = 0; // Reset time to beginning
				console.log(`Set time range from USD animation: 0s - ${firstClip.duration}s`);

				// Update GUI controllers if they exist
				updateTimeRangeGUIControllers(firstClip.duration);
			}

			// Play the first USD animation automatically
			playUSDAnimation(0);
		} else {
			// No USD animations found - use synthetic animations
			console.log('No USD animations found, using synthetic animations');
			animationParams.useUSDAnimation = false;

			// Hide USD animations folder
			if (window.usdAnimationFolder) {
				window.usdAnimationFolder.hide();
			}
			if (window.updateAnimationList) {
				window.updateAnimationList([], []);
			}

			// Initialize synthetic animation
			updateAnimationClip();
		}
	} catch (error) {
		console.log('No animations found in USD file or animation extraction not supported:', error);

		// Fallback to synthetic animations
		animationParams.useUSDAnimation = false;

		if (window.usdAnimationFolder) {
			window.usdAnimationFolder.hide();
		}
		if (window.updateAnimationList) {
			window.updateAnimationList([], []);
		}

		// Initialize synthetic animation
		updateAnimationClip();
	}
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

// Listen for default model reload
window.addEventListener('loadDefaultModel', async () => {
	try {
		await loadUSDModel();
		console.log('Default model reloaded');
	} catch (error) {
		console.error('Failed to reload default model:', error);
	}
});

// Load USD model and initialize animation
loadUSDModel().catch((error) => {
	console.error('Failed to load USD model:', error);
	// Fallback: create a simple cube if USD loading fails
	const cubeGeometry = new THREE.BoxGeometry(1, 1, 1);
	const cubeMaterial = new THREE.MeshStandardMaterial({
		color: 0xff6b6b,
		roughness: 0.5,
		metalness: 0.3
	});
	parentCube = new THREE.Mesh(cubeGeometry, cubeMaterial);
	parentCube.name = 'parentCube';
	parentCube.castShadow = true;
	parentCube.receiveShadow = true;
	parentCube.position.y = 0.5;
	parentGroup.add(parentCube);
	updateAnimationClip();
});

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

	// Update animation time with begin/end range
	if (animationParams.isPlaying) {
		animationParams.time += deltaTime * animationParams.speed;

		// Loop within begin/end range
		if (animationParams.time > animationParams.endTime) {
			animationParams.time = animationParams.beginTime;
			if (animationAction) {
				animationAction.time = animationParams.beginTime;
			}
		}
		if (animationParams.time < animationParams.beginTime) {
			animationParams.time = animationParams.beginTime;
		}
	}

	// Update the mixer for KeyframeTrack animations
	if (mixer && animationAction && animationParams.isPlaying) {
		mixer.update(deltaTime * animationParams.speed);
	}

	// Normalized time (0 to 1) within the begin/end range
	const rangeTime = animationParams.time - animationParams.beginTime;
	const rangeDuration = animationParams.endTime - animationParams.beginTime;
	const t = rangeDuration > 0 ? rangeTime / rangeDuration : 0;

	// Parent animations (only if no KeyframeTrack is controlling the parent)
	// Note: The cube is now controlled by KeyframeTracks, so we skip parent animations
	// These animations are for the group that contains other objects

	// Child animations
	if (animationParams.childRotation) {
		childGroup.rotation.y = t * Math.PI * 4; // Rotate twice as fast
	}

	if (animationParams.childOrbit) {
		const orbitRadius = 2;
		childGroup.position.x = Math.cos(t * Math.PI * 2) * orbitRadius;
		childGroup.position.z = Math.sin(t * Math.PI * 2) * orbitRadius;
	}

	// Grandchild animations
	if (animationParams.grandchildRotation) {
		grandchildGroup.rotation.z = t * Math.PI * 6; // Rotate three times as fast
	}

	// Update controls
	controls.update();

	// Render
	renderer.render(scene, camera);
}

// Start animation
animate();
