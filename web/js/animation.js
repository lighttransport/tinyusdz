import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';

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

// Parent cube (will be animated with KeyframeTracks)
const cubeGeometry = new THREE.BoxGeometry(1, 1, 1);
const cubeMaterial = new THREE.MeshStandardMaterial({
	color: 0xff6b6b,
	roughness: 0.5,
	metalness: 0.3
});
const parentCube = new THREE.Mesh(cubeGeometry, cubeMaterial);
parentCube.name = 'parentCube';
parentCube.castShadow = true;
parentCube.receiveShadow = true;
parentCube.position.y = 0.5;
parentGroup.add(parentCube);

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
const mixer = new THREE.AnimationMixer(parentCube);
let animationAction = null;
let currentTracks = createKeyframeTracks(10);
let enabledTracks = {
	position: true,
	rotation: true,
	scale: true
};

function updateAnimationClip() {
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

// Playback controls
const playbackFolder = gui.addFolder('Playback');
playbackFolder.add(animationParams, 'playPause').name('Play / Pause');
playbackFolder.add(animationParams, 'reset').name('Reset');
playbackFolder.add(animationParams, 'speed', 0, 3, 0.1).name('Speed');
playbackFolder.add(animationParams, 'time', 0, 30, 0.01)
	.name('Timeline').listen();
playbackFolder.open();

// Time range controls
const timeRangeFolder = gui.addFolder('Time Range');
timeRangeFolder.add(animationParams, 'beginTime', 0, 29, 0.1)
	.name('Begin Time (s)')
	.onChange(() => {
		if (animationParams.beginTime >= animationParams.endTime) {
			animationParams.beginTime = animationParams.endTime - 0.1;
		}
		animationParams.updateDuration();
	});
timeRangeFolder.add(animationParams, 'endTime', 0.1, 30, 0.1)
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

// Initialize animation after animationParams is defined
updateAnimationClip();

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
