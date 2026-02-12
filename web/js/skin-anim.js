import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TransformControls } from 'three/examples/jsm/controls/TransformControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
// USD Skeletal Animation Helper - skeleton building with bind-transform logic
import {
	createThreeSkeletonFromUSD,
	resetSkeletonToRestPose
} from 'tinyusdz/USDSkeletalHelper.js';
// USD Animation Converter - skeletal animation extraction
import { convertUSDSkeletalAnimationsToThreeJS } from 'tinyusdz/USDAnimationConverter.js';
// Skinned Mesh Utilities - bbox, raycasting, hierarchy helpers
import {
	findNodeByUSDPath,
	replaceWithSkinnedMesh,
	computeSceneBoundingBox,
	expandBoxByMeshBones,
	expandBoxBySkeletonBones,
	raycastSkinnedMeshes
} from 'tinyusdz/SkinnedMeshUtils.js';
// Extended Skinning Support - supports 4, 8, 16, 32, 64+ bones per vertex
import {
	SkinningMode,
	addExtendedSkinningAttributes,
	applyExtendedSkinningIfNeeded,
	createExtendedWeightVisualizationMaterial
} from 'tinyusdz/ExtendedSkinning.js';

// ===========================================
// Configuration
// ===========================================

// Scene setup
const scene = new THREE.Scene();
window._scene = scene; // Debug: expose for console access
scene.background = new THREE.Color(0x1a1a1a);

// Reusable temporaries to reduce per-frame GC pressure
const _tmpVec3 = new THREE.Vector3();
const _tmpVec3b = new THREE.Vector3();
const _tmpVec3c = new THREE.Vector3();
const _tmpBox3 = new THREE.Box3();
// =====================================================================
// CPU Skinning Debug Path
//
// Computes skinned vertex positions entirely on the CPU, bypassing the
// GPU skinning shader.  Useful for isolating shader bugs (e.g. wrong
// bone texture fetch, precision issues).
//
// When enabled, each frame:
//   1. Creates a non-skinned debug Mesh with cloned geometry
//   2. Computes: pos' = bindMatrixInverse * sum(w * boneMatrix * bindMatrix * pos)
//   3. Writes result to the debug mesh geometry
//   4. Hides the SkinnedMesh, shows the debug Mesh
// =====================================================================

let _cpuSkinEnabled = false;
let _rawMeshEnabled = false;

/** Remove CPU skinning debug meshes and restore skinned mesh visibility */
function _cleanupCpuSkin() {
	for (const mesh of allSceneMeshes) {
		if (mesh._cpuDebugMesh) {
			scene.remove(mesh._cpuDebugMesh);
			mesh._cpuDebugMesh.geometry.dispose();
			mesh._cpuDebugMesh.material.dispose();
			mesh._cpuDebugMesh = null;
			mesh._cpuOrigPos = null;
			mesh._cpuOrigNorm = null;
			mesh._cpuBoneMatrices = null;
		}
		if (mesh.isSkinnedMesh) {
			mesh.visible = animationParams.showMesh;
		}
	}
}

/** Remove raw mesh debug meshes and restore skinned mesh visibility */
function _cleanupRawMesh() {
	for (const mesh of allSceneMeshes) {
		if (mesh._rawDebugMesh) {
			scene.remove(mesh._rawDebugMesh);
			mesh._rawDebugMesh.geometry.dispose();
			mesh._rawDebugMesh.material.dispose();
			mesh._rawDebugMesh = null;
		}
		if (mesh.isSkinnedMesh) {
			mesh.visible = animationParams.showMesh;
		}
	}
}

/**
 * Hint V8 to run garbage collection.
 * Uses window.gc() if available (Chrome --expose-gc), otherwise creates a
 * short-lived large allocation to nudge the GC heuristics.
 */
function hintGC() {
	if (typeof window.gc === 'function') {
		window.gc();
	} else {
		// Allocate and immediately discard — pushes V8 past its allocation threshold.
		// 16MB is enough to nudge V8's minor GC without wasting memory on the hint itself.
		void new ArrayBuffer(16 * 1024 * 1024);
	}
}

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
let _lastMixerUpdateTime = 0; // For mixer update throttling
let _mixerAccumDelta = 0; // Accumulated delta for throttled updates


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
// App-specific Functions
// ===========================================

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
		bone.getWorldPosition(_tmpVec3);
		sphere.position.copy(_tmpVec3);
	});
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

function applyCameraFit(box, paddingFactor, label) {
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

	// Adapt controls to scene scale
	controls.minDistance = maxDim * 0.05;
	controls.maxDistance = maxDim * 50;
	controls.zoomSpeed = 3.0;
	controls.panSpeed = 2.0;
	controls.update();

	camera.near = Math.max(0.01, maxDim * 0.001);
	camera.far = maxDim * 100;
	camera.updateProjectionMatrix();

	console.log(`fitCameraToScene (${label}): center=(${_tmpVec3.x.toFixed(2)}, ${_tmpVec3.y.toFixed(2)}, ${_tmpVec3.z.toFixed(2)}), size=(${_tmpVec3b.x.toFixed(2)}, ${_tmpVec3b.y.toFixed(2)}, ${_tmpVec3b.z.toFixed(2)}), distance=${cameraDistance.toFixed(2)}, maxDim=${maxDim.toFixed(2)}`);
}

function fitCameraToScene(targetObject = null, paddingFactor = 1.5) {
	const target = targetObject || usdSceneRoot;
	if (!target || target.children.length === 0) {
		console.warn('fitCameraToScene: No object to fit');
		return;
	}
	target.updateMatrixWorld(true);

	// Compute bounding box at current frame from visible skinned meshes' bone positions
	const box = new THREE.Box3();
	let hasMeshBones = false;
	for (const mesh of allSceneMeshes) {
		if (!mesh.visible) continue;
		if (mesh.isSkinnedMesh && mesh.skeleton) {
			expandBoxByMeshBones(mesh, box);
			hasMeshBones = true;
		}
	}
	if (!hasMeshBones) box.expandByObject(target);

	if (box.isEmpty()) {
		console.warn('fitCameraToScene: Could not compute bounding box');
		return;
	}
	applyCameraFit(box, paddingFactor, 'current frame');
}

function fitCameraToAllFrames(targetObject = null, paddingFactor = 1.5) {
	const target = targetObject || usdSceneRoot;
	if (!target || target.children.length === 0) {
		console.warn('fitCameraToAllFrames: No object to fit');
		return;
	}
	target.updateMatrixWorld(true);

	const box = new THREE.Box3();
	const hasAnimation = mixer && usdAnimations.length > 0 && animationAction;

	if (hasAnimation && allSceneMeshes.length > 0) {
		const clip = usdAnimations[animationParams.currentAnimation] || usdAnimations[0];
		const duration = clip.duration;
		const boneCount = skinnedMesh?.skeleton?.bones?.length || 0;
		const numSamples = boneCount > 500 ? 5 : 20;
		const savedTime = animationAction.time;

		console.log(`fitCameraToAllFrames: Sampling ${numSamples} frames over ${duration.toFixed(2)}s (${boneCount} bones)`);

		for (let i = 0; i <= numSamples; i++) {
			const sampleTime = (i / numSamples) * duration;
			animationAction.time = sampleTime;
			mixer.setTime(sampleTime);
			if (skinnedMesh?.skeleton) skinnedMesh.skeleton.update();
			target.updateMatrixWorld(true);
			for (const mesh of allSceneMeshes) {
				if (!mesh.visible) continue;
				if (mesh.isSkinnedMesh && mesh.skeleton) {
					expandBoxByMeshBones(mesh, box);
				}
			}
		}

		// Restore original time
		animationAction.time = savedTime;
		mixer.setTime(savedTime);
		if (skinnedMesh?.skeleton) skinnedMesh.skeleton.update();
		target.updateMatrixWorld(true);
	} else {
		// No animation — fall back to current frame
		for (const mesh of allSceneMeshes) {
			if (!mesh.visible) continue;
			if (mesh.isSkinnedMesh && mesh.skeleton) {
				expandBoxByMeshBones(mesh, box);
			}
		}
		if (box.isEmpty()) box.expandByObject(target);
	}

	if (box.isEmpty()) {
		console.warn('fitCameraToAllFrames: Could not compute bounding box');
		return;
	}
	applyCameraFit(box, paddingFactor, 'all frames');
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
	const usd_filename = "./assets/skintest-animated.usda";

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

			// Copy WASM typed_memory_view data into JS-owned buffers.
			// usd_scene.delete() at end of processUSDScene frees C++ render_scene_,
			// invalidating all typed_memory_view references into that data.
			const meshData = {
				meshId: i,
				jointIndices: new Int32Array(mesh.jointIndices),
				jointWeights: new Float32Array(mesh.jointWeights),
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
	let cachedBoneInverses = [];

	if (numSkeletons > 0) {
		// Get first skeleton (for simplicity, only support one skeleton for now)
		const usdSkeleton = usd_scene.getSkeleton(0);
		skeletonAbsPath = usdSkeleton.abs_path || null;
		console.log('USD Skeleton:', usdSkeleton);

		// Build Three.js skeleton from USD data (bind-transform based)
		const skeletonData = createThreeSkeletonFromUSD(usdSkeleton);
		bones = skeletonData.bones;
		boneMap = skeletonData.boneMap;
		rootBone = skeletonData.rootBone;
		cachedBoneInverses = skeletonData.boneInverses;
		console.log(`Built skeleton with ${bones.length} bones`);

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
		const boneInverses = cachedBoneInverses;
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

			// Joint spheres are created lazily on first showJoints toggle
			// to avoid 3000+ sphere meshes + materials bloating memory/scene graph

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
			usdAnimations = convertUSDSkeletalAnimationsToThreeJS(usd_scene, boneMap, timeCodesPerSecond);
			console.log(`Converted ${usdAnimations.length} animations (fps: ${timeCodesPerSecond})`);
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

	// Release WASM scene object — all USD data must be copied into JS-owned buffers
	// BEFORE this point. The C++ destructor frees render_scene_ vectors, invalidating
	// all typed_memory_view references (mesh.points, sampler.times/values, etc.).
	// Keeping it alive retains the entire parsed USD scene in WASM heap memory.
	if (usd_scene && typeof usd_scene.delete === 'function') {
		usd_scene.delete();
	}
	window.usd_scene = null;

	// Hint GC after scene loading — processUSDScene creates many transient objects
	// (WASM data copies, temporary matrices, skeleton building intermediaries) that
	// should be collected before the animation loop starts allocating.
	hintGC();
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
		if (!this.isPlaying) {
			_mixerAccumDelta = 0;
			hintGC();
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
		_mixerAccumDelta = 0;
		hintGC();
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
			mesh.castShadow = mesh.visible;
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
	fitToAllFrames: function() {
		fitCameraToAllFrames();
	},

	// Debug visualization
	showJoints: false,
	toggleJoints: function() {
		if (this.showJoints) {
			// Lazy-create joint spheres on first toggle
			if (jointSpheres.length === 0 && skeleton && skeleton.bones.length > 0) {
				jointSpheres = createJointSpheres(skeleton.bones);
			}
			jointSpheres.forEach(sphere => {
				sphere.visible = true;
				if (!sphere.parent) scene.add(sphere);
			});
		} else {
			// Remove from scene to avoid bloating scene graph traversal
			jointSpheres.forEach(sphere => scene.remove(sphere));
		}
	},

	// CPU skinning debug mode
	cpuSkinning: false,
	toggleCPUSkinning: function() {
		_cpuSkinEnabled = this.cpuSkinning;
		if (_cpuSkinEnabled) {
			// Disable raw mesh if enabling CPU skinning
			this.rawMesh = false;
			_rawMeshEnabled = false;
			_cleanupRawMesh();
		}
		if (!_cpuSkinEnabled) {
			_cleanupCpuSkin();
			console.log('CPU skinning disabled, GPU skinning restored');
		} else {
			console.log('CPU skinning enabled — bypassing GPU shader');
		}
	},

	// Raw mesh (no skinning) debug mode
	rawMesh: false,
	toggleRawMesh: function() {
		_rawMeshEnabled = this.rawMesh;
		if (_rawMeshEnabled) {
			// Disable CPU skinning if enabling raw mesh
			this.cpuSkinning = false;
			_cpuSkinEnabled = false;
			_cleanupCpuSkin();
		}
		if (!_rawMeshEnabled) {
			_cleanupRawMesh();
			console.log('Raw mesh disabled, GPU skinning restored');
		} else {
			console.log('Raw mesh enabled — showing original geometry (no skinning)');
		}
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

	// Extended skinning toggle (texture-based vs 4-bone fallback)
	useExtendedSkinning: true,
	toggleExtendedSkinning: function() {
		const enabled = this.useExtendedSkinning ? 1.0 : 0.0;
		for (const mesh of allSceneMeshes) {
			if (mesh._useTexSkinUniform) {
				mesh._useTexSkinUniform.value = enabled;
			}
		}
		console.log(`Extended skinning: ${this.useExtendedSkinning ? 'texture-based' : '4-bone fallback'}`);
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
		mesh.castShadow = mesh.visible;
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
visualFolder.add(animationParams, 'useExtendedSkinning')
	.name('Extended Skinning')
	.onChange(() => animationParams.toggleExtendedSkinning());
visualFolder.add(animationParams, 'convertZUp')
	.name('Z-up → Y-up')
	.onChange(() => animationParams.toggleZUp())
	.listen();
visualFolder.add(animationParams, 'fitToScene').name('Fit (Current Frame)');
visualFolder.add(animationParams, 'fitToAllFrames').name('Fit (All Frames)');
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
debugFolder.add(animationParams, 'cpuSkinning')
	.name('CPU Skinning')
	.listen()
	.onChange(() => animationParams.toggleCPUSkinning());
debugFolder.add(animationParams, 'rawMesh')
	.name('Raw Mesh (No Skin)')
	.listen()
	.onChange(() => animationParams.toggleRawMesh());
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
	objects: scene.children.length,
	heapMB: 0
};
infoFolder.add(info, 'fps').name('FPS').listen().disable();
infoFolder.add(info, 'objects').name('Objects').listen().disable();
if (performance.memory) {
	infoFolder.add(info, 'heapMB').name('Heap (MB)').listen().disable();
}
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
let _lastGCHintTime = 0; // For periodic GC hints during playback

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
		if (performance.memory) {
			info.heapMB = Math.round(performance.memory.usedJSHeapSize / (1024 * 1024));
		}
		frames = 0;
		fpsUpdateTime = 0;
	}

	// Update animation mixer
	// Speed represents playback FPS (timeCodes per second)
	// Animation times are in timeCodes, so speed directly controls how many timeCodes advance per second
	if (mixer && animationParams.isPlaying) {
		// Throttle mixer updates to ~30fps for ALL animated skeletons to reduce GC pressure.
		// mixer.update() allocates transient objects per call (proportional to track count);
		// at 60fps the allocation churn triggers frequent minor GC pauses even for small skeletons.
		// 30fps mixer updates are visually indistinguishable but halve the allocation rate.
		const mixerInterval = 1 / 30;
		_mixerAccumDelta += deltaTime;

		if (_mixerAccumDelta >= mixerInterval) {
			mixer.update(_mixerAccumDelta);
			_mixerAccumDelta = 0;

			// Update time display
			if (animationAction) {
				animationParams.time = animationAction.time;
			}
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

	// CPU skinning debug path: compute positions on CPU, render via a
	// separate non-skinned debug mesh (avoids fighting Three.js GPU pipeline)
	if (_cpuSkinEnabled && skeleton) {
		scene.updateMatrixWorld(true);
		skeleton.update();

		for (const mesh of allSceneMeshes) {
			if (!mesh.isSkinnedMesh || !mesh.skeleton) continue;

			// Create debug mesh lazily
			if (!mesh._cpuDebugMesh) {
				const debugGeo = mesh.geometry.clone();
				const debugMat = mesh.material.clone();
				const debugMesh = new THREE.Mesh(debugGeo, debugMat);
				debugMesh.name = mesh.name + '_cpuSkin';
				debugMesh.castShadow = true;
				debugMesh.receiveShadow = true;
				// Place at mesh's world transform
				debugMesh.matrixAutoUpdate = false;
				mesh._cpuDebugMesh = debugMesh;
				// Snapshot original positions from the source geometry
				mesh._cpuOrigPos = new Float32Array(mesh.geometry.attributes.position.array);
				if (mesh.geometry.attributes.normal) {
					mesh._cpuOrigNorm = new Float32Array(mesh.geometry.attributes.normal.array);
				}
				scene.add(debugMesh);
			}

			// Sync world transform
			mesh._cpuDebugMesh.matrix.copy(mesh.matrixWorld);
			mesh._cpuDebugMesh.matrixWorld.copy(mesh.matrixWorld);
			mesh._cpuDebugMesh.visible = animationParams.showMesh;
			mesh.visible = false; // hide GPU-skinned mesh

			// Compute CPU skinning into debug mesh geometry
			const srcGeo = mesh.geometry;
			const dstGeo = mesh._cpuDebugMesh.geometry;
			const srcPos = mesh._cpuOrigPos;
			const dstPosAttr = dstGeo.attributes.position;
			const dstNormAttr = dstGeo.attributes.normal;
			const skinIdx = srcGeo.attributes.skinIndex;
			const skinWt  = srcGeo.attributes.skinWeight;
			const skel = mesh.skeleton;

			const bindMat = mesh.bindMatrix;
			const bindMatInv = mesh.bindMatrixInverse;

			// Pre-compute boneMatrix[b] = bone.matrixWorld * boneInverses[b]
			if (!mesh._cpuBoneMatrices) {
				mesh._cpuBoneMatrices = [];
				for (let b = 0; b < skel.bones.length; b++) {
					mesh._cpuBoneMatrices.push(new THREE.Matrix4());
				}
			}
			for (let b = 0; b < skel.bones.length; b++) {
				mesh._cpuBoneMatrices[b].multiplyMatrices(skel.bones[b].matrixWorld, skel.boneInverses[b]);
			}

			const vSrc = new THREE.Vector4();
			const vTmp = new THREE.Vector3();
			const vOut = new THREE.Vector3();

			for (let i = 0; i < dstPosAttr.count; i++) {
				// bindMatrix * original_pos
				vSrc.set(srcPos[i*3], srcPos[i*3+1], srcPos[i*3+2], 1).applyMatrix4(bindMat);

				vOut.set(0, 0, 0);
				for (let j = 0; j < skinIdx.itemSize; j++) {
					const bi = skinIdx.getComponent(i, j);
					const wt = skinWt.getComponent(i, j);
					if (wt === 0) continue;
					if (bi >= 0 && bi < skel.bones.length) {
						vTmp.set(vSrc.x, vSrc.y, vSrc.z).applyMatrix4(mesh._cpuBoneMatrices[bi]);
						vOut.addScaledVector(vTmp, wt);
					}
				}

				// bindMatrixInverse * accumulated
				vOut.applyMatrix4(bindMatInv);

				dstPosAttr.setXYZ(i, vOut.x, vOut.y, vOut.z);
			}

			dstPosAttr.needsUpdate = true;
			dstGeo.computeVertexNormals(); // recompute normals from new positions
			dstGeo.computeBoundingSphere();
		}
	}

	// Raw mesh debug path: show original geometry with mesh's world transform,
	// no skinning applied at all.  Useful to verify mesh data is correct.
	if (_rawMeshEnabled) {
		scene.updateMatrixWorld(true);

		for (const mesh of allSceneMeshes) {
			if (!mesh.isSkinnedMesh) continue;

			// Create raw debug mesh lazily
			if (!mesh._rawDebugMesh) {
				const debugGeo = mesh.geometry.clone();
				const debugMat = mesh.material.clone();
				const debugMesh = new THREE.Mesh(debugGeo, debugMat);
				debugMesh.name = mesh.name + '_rawMesh';
				debugMesh.castShadow = true;
				debugMesh.receiveShadow = true;
				debugMesh.matrixAutoUpdate = false;
				mesh._rawDebugMesh = debugMesh;
				scene.add(debugMesh);
				console.log(`Raw mesh created: ${debugMesh.name}, ${debugGeo.attributes.position.count} verts`);
			}

			// Position at mesh's world transform (includes hierarchy: Z_UP, Armature, etc.)
			mesh._rawDebugMesh.matrix.copy(mesh.matrixWorld);
			mesh._rawDebugMesh.matrixWorld.copy(mesh.matrixWorld);
			mesh._rawDebugMesh.visible = animationParams.showMesh;
			mesh.visible = false; // hide GPU-skinned mesh
		}
	}

	// Update controls
	controls.update();

	// Update object count
	info.objects = scene.children.length;

	// Render
	renderer.render(scene, camera);

	// Periodic GC hint during animation playback.
	// mixer.update() generates transient allocations each frame; without periodic GC,
	// V8 accumulates garbage and eventually triggers a long pause or heap growth.
	if (animationParams.isPlaying && skeleton) {
		if (currentTime - _lastGCHintTime > 10000) { // every 10 seconds
			_lastGCHintTime = currentTime;
			hintGC();
		}
	}
}

// Start animation loop
animate();
