/**
 * anim-clips.js — Animation Clip Mixing Demo
 *
 * Per-object animation controls: click a mesh to select its animated object,
 * then solo/crossfade/blend clips independently per object.
 * Supports multi-object USD scenes (armature + camera + light + mesh).
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import {
	createConfiguredTinyUSDZLoader,
	loadUSDSceneFromURL,
	parseUSDSceneFromArrayBuffer
} from 'tinyusdz/LoaderConfigUtils.js';
import { getUSDSceneMetadata } from 'tinyusdz/USDSceneMetadata.js';
import { buildSkeletonDataFromUSD } from 'tinyusdz/USDSkeletonData.js';
import { extractSkinnedMeshData } from 'tinyusdz/USDSceneSkinningData.js';
import { applyUSDSceneSkinningPipeline } from 'tinyusdz/USDSceneSkinningPipeline.js';
import { buildNodeIndexMap } from 'tinyusdz/USDAnimationConverter.js';
import { extractUSDSceneAnimations } from 'tinyusdz/USDSceneAnimationPipeline.js';
import {
	crossfadeActions,
	prepareClipsForBlending,
	soloAction,
	setActionWeights
} from 'tinyusdz/AnimClipUtils.js';
import { raycastSkinnedMeshes, expandBoxByMeshBones } from 'tinyusdz/SkinnedMeshUtils.js';
import { attachSceneHelpers } from 'tinyusdz/SceneHelpers.js';

// =====================================================
// Three.js Scene Setup
// =====================================================

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1a1a2e);

const camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.01, 100);
camera.position.set(0, 2, 5);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setPixelRatio(window.devicePixelRatio);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 1, 0);
controls.update();

// Lights
const ambientLight = new THREE.AmbientLight(0x404060, 1.5);
scene.add(ambientLight);

const dirLight = new THREE.DirectionalLight(0xffffff, 2.0);
dirLight.position.set(3, 5, 3);
dirLight.castShadow = true;
dirLight.shadow.mapSize.set(1024, 1024);
dirLight.shadow.camera.near = 0.1;
dirLight.shadow.camera.far = 20;
dirLight.shadow.camera.left = -5;
dirLight.shadow.camera.right = 5;
dirLight.shadow.camera.top = 5;
dirLight.shadow.camera.bottom = -5;
dirLight.shadow.bias = -0.001;
scene.add(dirLight);

// Ground plane — polygonOffset pushes it behind co-planar grid lines
const groundGeo = new THREE.PlaneGeometry(20, 20);
const groundMat = new THREE.MeshStandardMaterial({
	color: 0x222233, roughness: 0.9,
	polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1,
});
const ground = new THREE.Mesh(groundGeo, groundMat);
ground.rotation.x = -Math.PI / 2;
ground.receiveShadow = true;
scene.add(ground);

// Grid — normal depth test so scene meshes correctly occlude it
const gridHelper = new THREE.GridHelper(10, 20, 0x444466, 0x333355);
scene.add(gridHelper);

// =====================================================
// State
// =====================================================

let mixer = null;
const allClips = [];    // THREE.AnimationClip[]
const allActions = [];  // THREE.AnimationAction[]
let activeIndex = 0;
let blendMode = 'solo'; // 'solo' | 'blend'
let crossfadeDuration = 0.5;
let isPlaying = true;
let playbackSpeed = 1.0;
let sceneTimeCodesPerSecond = 24; // USD timeCodesPerSecond (tracks store frame numbers)

const characterGroup = new THREE.Group();
characterGroup.name = 'characterGroup';
scene.add(characterGroup);

let allSceneMeshes = [];
let skeletons = new Map();
let skeletonHelpers = [];

// Animation info from USD (metadata per clip)
let animationInfos = [];

// Per-object animation state
const objectAnimMap = new Map(); // objectId -> ObjectAnimState
let selectedObjectId = null;
let filterMode = 'all'; // 'all' | 'selected'

// Raycasting
const raycaster = new THREE.Raycaster();
const mouse = new THREE.Vector2();
let _mouseDownPos = { x: 0, y: 0 };

// Selection highlight
let selectionHelper = null;

const clock = new THREE.Clock();

// =====================================================
// Process USD Scene
// =====================================================

async function processUSDScene(usdScene, filename) {
	const displayName = filename.split('/').pop();
	const currentFileEl = document.getElementById('currentFile');
	if (currentFileEl) currentFileEl.textContent = displayName;

	// Dispose previous state
	if (mixer) {
		mixer.stopAllAction();
		mixer.uncacheRoot(mixer.getRoot());
		mixer = null;
	}
	allClips.length = 0;
	allActions.length = 0;
	activeIndex = 0;
	animationInfos = [];
	objectAnimMap.clear();
	selectedObjectId = null;
	filterMode = 'all';
	removeSelectionHighlight();

	for (const mesh of allSceneMeshes) {
		if (mesh.geometry) mesh.geometry.dispose();
		if (mesh.material) {
			if (Array.isArray(mesh.material)) mesh.material.forEach(m => m.dispose());
			else mesh.material.dispose();
		}
		if (mesh.customDepthMaterial) mesh.customDepthMaterial.dispose();
	}
	for (const [, skel] of skeletons) {
		if (skel && skel.boneTexture) skel.boneTexture.dispose();
	}
	for (const helper of skeletonHelpers) {
		scene.remove(helper);
		if (helper.geometry) helper.geometry.dispose();
		if (helper.material) {
			if (Array.isArray(helper.material)) helper.material.forEach(m => m.dispose());
			else helper.material.dispose();
		}
	}
	allSceneMeshes = [];
	skeletons.clear();
	skeletonHelpers = [];

	// Reset characterGroup
	characterGroup.position.set(0, 0, 0);
	characterGroup.quaternion.identity();
	characterGroup.scale.set(1, 1, 1);
	while (characterGroup.children.length > 0) {
		characterGroup.remove(characterGroup.children[0]);
	}

	// Get metadata
	const {
		fileUpAxis,
		timeCodesPerSecond
	} = getUSDSceneMetadata(usdScene);

	sceneTimeCodesPerSecond = timeCodesPerSecond || 24;

	const {
		hasSkinnedMeshData,
		allSkinnedMeshUSDData,
		skinnedMeshDataByName
	} = extractSkinnedMeshData(usdScene, { logger: console });

	// Build skeleton data
	const skeletonBuild = buildSkeletonDataFromUSD(usdScene, {
		logger: console,
		hasSkinnedMeshData
	});
	const skeletonDataArray = skeletonBuild.skeletonDataArray;
	const boneMaps = skeletonBuild.boneMaps;

	// Build Three.js scene graph
	const usdRootNode = usdScene.getDefaultRootNode();
	const defaultMtl = TinyUSDZLoaderUtils.createDefaultMaterial();
	const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(
		usdRootNode, defaultMtl, usdScene, { overrideMaterial: false }
	);

	// Build node index map before skinning pipeline
	const nodeIndexMap = buildNodeIndexMap(threeNode);

	// Apply Z-up to Y-up conversion
	if (fileUpAxis.toUpperCase() === 'Z') {
		characterGroup.rotation.x = -Math.PI / 2;
	}

	// Apply skinning pipeline
	const skinningResult = applyUSDSceneSkinningPipeline({
		threeNode,
		characterGroup,
		helperScene: scene,
		skeletonDataArray,
		allSkinnedMeshUSDData,
		skinnedMeshDataByName,
		usdScene,
		showMesh: true,
		showSkeleton: true,
		useWASMBoneTexture: false,
		logger: console
	});

	skeletons = skinningResult.skeletons;
	skeletonHelpers = skinningResult.skeletonHelpers;
	allSceneMeshes = skinningResult.allSceneMeshes;

	// Attach Blender-style visual helpers for lights and cameras
	const box = new THREE.Box3().setFromObject(characterGroup);
	const maxDim = box.getSize(new THREE.Vector3()).length();
	const helperScale = Math.max(maxDim * 0.15, 0.2);
	attachSceneHelpers(threeNode, usdScene, { scale: helperScale });

	// Extract animations
	try {
		const animData = extractUSDSceneAnimations(usdScene, {
			boneMaps,
			nodeIndexMap,
			timeCodesPerSecond,
			logger: console
		});

		animationInfos = animData.animationInfos || [];

		// Combine skeletal + node clips
		const combinedClips = [
			...animData.usdAnimations,
			...animData.usdNodeAnimations
		];

		allClips.push(...combinedClips);
		console.log(`Loaded ${allClips.length} animation clip(s)`);

		allClips.forEach((clip, i) => {
			const info = animationInfos[i];
			const srcType = info?.sourceType || 'Unknown';
			const joints = info?.numAnimatedJoints ?? 0;
			console.log(`  Clip ${i}: "${clip.name}" — ${clip.duration.toFixed(2)}s, ${clip.tracks.length} tracks, source: ${srcType}, joints: ${joints}`);
		});
	} catch (err) {
		console.error('Animation extraction failed:', err);
	}

	// Delete WASM scene (data has been copied into JS-owned buffers)
	usdScene.delete();

	// Create mixer and prepare actions
	if (allClips.length > 0) {
		mixer = new THREE.AnimationMixer(characterGroup);
		// Track times are in USD timecodes (frame numbers), not seconds.
		// Set mixer timeScale to convert frame-time deltas to real-time.
		mixer.timeScale = sceneTimeCodesPerSecond * playbackSpeed;
		allActions.push(...prepareClipsForBlending(mixer, allClips));

		// Build per-object clip mapping
		buildObjectAnimMap();

		// Solo the first clip in each object by default
		for (const [, state] of objectAnimMap) {
			state.activeClipIndex = 0;
			state.isPlaying = true;
			// Solo the first clip within this object
			for (let i = 0; i < state.actions.length; i++) {
				state.actions[i].enabled = true;
				state.actions[i].setEffectiveWeight(i === 0 ? 1 : 0);
				if (i === 0) state.actions[i].play();
			}
		}
		activeIndex = 0;
		blendMode = 'solo';
		isPlaying = true;
	}

	// Update UI
	renderClipPanel();

	// Fit camera to scene
	fitCamera();
}

// =====================================================
// Camera Fitting
// =====================================================

function fitCamera() {
	const box = new THREE.Box3().setFromObject(characterGroup);
	if (box.isEmpty()) return;

	const center = box.getCenter(new THREE.Vector3());
	const size = box.getSize(new THREE.Vector3());
	const maxDim = Math.max(size.x, size.y, size.z);
	const dist = maxDim * 2.5;

	controls.target.copy(center);
	camera.position.set(center.x + dist * 0.5, center.y + dist * 0.4, center.z + dist);
	controls.update();
}

// =====================================================
// Object-Clip Mapping
// =====================================================

function buildObjectAnimMap() {
	objectAnimMap.clear();

	if (allClips.length === 0 || allActions.length === 0) return;

	// 1. Build boneNameToSkelId map
	const boneNameToSkelId = new Map();
	for (const [skelId, skeleton] of skeletons) {
		for (const bone of skeleton.bones) {
			boneNameToSkelId.set(bone.name, skelId);
		}
	}

	// 2. Build skelIdToMeshes map
	const skelIdToMeshes = new Map();
	for (const mesh of allSceneMeshes) {
		if (mesh.isSkinnedMesh && mesh.skeleton) {
			// Find which skeleton this mesh is bound to
			for (const [skelId, skeleton] of skeletons) {
				if (mesh.skeleton === skeleton) {
					if (!skelIdToMeshes.has(skelId)) skelIdToMeshes.set(skelId, []);
					skelIdToMeshes.get(skelId).push(mesh);
					break;
				}
			}
		}
	}

	// 3. Build uuidToObject map by traversing characterGroup
	const uuidToObject = new Map();
	const nameToObject = new Map();
	characterGroup.traverse((obj) => {
		uuidToObject.set(obj.uuid, obj);
		if (obj.name) nameToObject.set(obj.name, obj);
	});

	// 4. For each clip, determine its owner object
	for (let clipIdx = 0; clipIdx < allClips.length; clipIdx++) {
		const clip = allClips[clipIdx];
		const action = allActions[clipIdx];
		let ownerId = null;
		let ownerType = 'node';
		let ownerMeshes = [];
		let ownerDisplayName = clip.name || 'Unnamed';

		// Parse track target names to find the owner
		for (const track of clip.tracks) {
			const dotIdx = track.name.indexOf('.');
			const targetName = dotIdx >= 0 ? track.name.substring(0, dotIdx) : track.name;

			// Check if target is a bone
			if (boneNameToSkelId.has(targetName)) {
				const skelId = boneNameToSkelId.get(targetName);
				ownerId = 'skeleton_' + skelId;
				ownerType = 'skeleton';
				ownerMeshes = skelIdToMeshes.get(skelId) || [];
				// Use first mesh name or skeleton id for display
				if (ownerMeshes.length > 0) {
					ownerDisplayName = ownerMeshes[0].name || ('Skeleton ' + skelId);
				} else {
					ownerDisplayName = 'Skeleton ' + skelId;
				}
				break;
			}

			// Check if target is a UUID (Three.js generates UUID-based track names)
			if (uuidToObject.has(targetName)) {
				const obj = uuidToObject.get(targetName);
				ownerId = 'node_' + obj.uuid;
				ownerType = 'node';
				ownerDisplayName = obj.name || 'Object';
				// Collect meshes from this object and its descendants
				ownerMeshes = [];
				if (obj.isMesh) {
					ownerMeshes.push(obj);
				} else {
					obj.traverse((child) => {
						if (child.isMesh) ownerMeshes.push(child);
					});
				}
				break;
			}

			// Check if target is a node name
			if (nameToObject.has(targetName)) {
				const obj = nameToObject.get(targetName);
				ownerId = 'node_' + obj.uuid;
				ownerType = 'node';
				ownerDisplayName = obj.name || 'Object';
				ownerMeshes = [];
				if (obj.isMesh) {
					ownerMeshes.push(obj);
				} else {
					obj.traverse((child) => {
						if (child.isMesh) ownerMeshes.push(child);
					});
				}
				break;
			}
		}

		// Fallback: unassigned clips go to a catch-all group
		if (!ownerId) {
			ownerId = '_unassigned';
			ownerType = 'node';
			ownerDisplayName = 'Other';
			ownerMeshes = [];
		}

		// Create or update object state
		if (!objectAnimMap.has(ownerId)) {
			objectAnimMap.set(ownerId, {
				objectId: ownerId,
				displayName: ownerDisplayName,
				type: ownerType,
				meshes: ownerMeshes,
				clips: [],
				clipIndices: [],
				actions: [],
				activeClipIndex: 0,
				isPlaying: true,
				blendMode: 'solo',
			});
		}

		const state = objectAnimMap.get(ownerId);
		state.clips.push(clip);
		state.clipIndices.push(clipIdx);
		state.actions.push(action);
	}

	console.log(`Built object-anim map: ${objectAnimMap.size} object(s)`);
	for (const [id, state] of objectAnimMap) {
		console.log(`  ${id}: "${state.displayName}" — ${state.clips.length} clip(s), ${state.meshes.length} mesh(es)`);
	}
}

// =====================================================
// Mouse Picking
// =====================================================

function onMouseDown(event) {
	_mouseDownPos.x = event.clientX;
	_mouseDownPos.y = event.clientY;
}

function onClick(event) {
	// Ignore clicks on UI panels
	if (event.target && event.target.closest && event.target.closest('#clip-panel, #info')) return;

	// Drag threshold — skip if user was orbiting
	const dx = event.clientX - _mouseDownPos.x;
	const dy = event.clientY - _mouseDownPos.y;
	if (dx * dx + dy * dy > 9) return;

	// Compute NDC
	mouse.x = (event.clientX / window.innerWidth) * 2 - 1;
	mouse.y = -(event.clientY / window.innerHeight) * 2 + 1;
	raycaster.setFromCamera(mouse, camera);

	const hits = raycastSkinnedMeshes(raycaster, allSceneMeshes);
	if (hits.length > 0) {
		selectObjectByMesh(hits[0].object);
	} else {
		deselectObject();
	}
}

function selectObjectByMesh(mesh) {
	for (const [objectId, state] of objectAnimMap) {
		if (state.meshes.includes(mesh)) {
			selectObject(objectId);
			return;
		}
	}
	// Mesh not found in any object group — deselect
	deselectObject();
}

function selectObject(objectId) {
	if (!objectAnimMap.has(objectId)) return;
	selectedObjectId = objectId;
	renderClipPanel();
}

function deselectObject() {
	selectedObjectId = null;
	removeSelectionHighlight();
	renderClipPanel();
}

renderer.domElement.addEventListener('mousedown', onMouseDown);
renderer.domElement.addEventListener('click', onClick);

// =====================================================
// Selection Highlight
// =====================================================

const _highlightBox = new THREE.Box3();

function updateSelectionHighlight() {
	if (!selectedObjectId || !objectAnimMap.has(selectedObjectId)) {
		removeSelectionHighlight();
		return;
	}

	const state = objectAnimMap.get(selectedObjectId);
	if (state.meshes.length === 0) {
		removeSelectionHighlight();
		return;
	}

	_highlightBox.makeEmpty();
	for (const mesh of state.meshes) {
		if (!mesh.visible) continue;
		if (mesh.isSkinnedMesh && mesh.skeleton) {
			expandBoxByMeshBones(mesh, _highlightBox);
		} else {
			_highlightBox.expandByObject(mesh);
		}
	}

	if (_highlightBox.isEmpty()) {
		removeSelectionHighlight();
		return;
	}

	// Add 10% padding
	const size = _highlightBox.getSize(new THREE.Vector3());
	const pad = size.multiplyScalar(0.1);
	_highlightBox.min.sub(pad);
	_highlightBox.max.add(pad);

	if (!selectionHelper) {
		selectionHelper = new THREE.Box3Helper(new THREE.Box3(), 0x4CAF50);
		scene.add(selectionHelper);
	}
	selectionHelper.box.copy(_highlightBox);
}

function removeSelectionHighlight() {
	if (selectionHelper) {
		scene.remove(selectionHelper);
		if (selectionHelper.geometry) selectionHelper.geometry.dispose();
		if (selectionHelper.material) selectionHelper.material.dispose();
		selectionHelper = null;
	}
}

// =====================================================
// UI Rendering
// =====================================================

function renderClipPanel() {
	const content = document.getElementById('clip-content');
	if (!content) return;

	if (allClips.length === 0) {
		content.innerHTML = '<p style="color: #888;">No clips loaded</p>';
		return;
	}

	let html = '';

	// Filter bar (only show if there are multiple objects)
	if (objectAnimMap.size > 1) {
		html += '<div class="filter-bar">';
		html += `<button class="${filterMode === 'all' ? 'active' : ''}" onclick="window._setFilter('all')">All</button>`;
		html += `<button class="${filterMode === 'selected' ? 'active' : ''}" onclick="window._setFilter('selected')">Selected Only</button>`;
		html += '</div>';
	}

	// Crossfade duration
	html += '<div class="crossfade-dur">';
	html += `Xfade: <input type="range" min="0.1" max="2.0" step="0.1" value="${crossfadeDuration}" id="crossfade-slider"> `;
	html += `<span id="crossfade-val">${crossfadeDuration.toFixed(1)}s</span>`;
	html += '</div>';

	// Render each object group
	for (const [objectId, state] of objectAnimMap) {
		// Apply filter
		if (filterMode === 'selected' && selectedObjectId && objectId !== selectedObjectId) continue;

		const isSelected = (objectId === selectedObjectId);
		html += `<div class="object-group${isSelected ? ' selected' : ''}">`;

		// Object header
		html += `<div class="object-header" onclick="window._selectObject('${objectId}')">`;
		html += `<span><span class="obj-name">${state.displayName}</span>`;
		html += `<span class="obj-type">${state.type === 'skeleton' ? 'Skel' : 'Node'}</span></span>`;
		html += `<span style="font-size:10px;color:#888;">${state.clips.length} clip${state.clips.length !== 1 ? 's' : ''}</span>`;
		html += '</div>';

		// Per-object controls
		html += '<div class="object-controls">';
		html += `<button onclick="window._togglePlayObject('${objectId}')">${state.isPlaying ? 'Pause' : 'Play'}</button>`;
		if (state.clips.length > 1) {
			html += `<button class="${state.blendMode !== 'blend' ? 'active' : ''}" onclick="window._setObjectMode('${objectId}','solo')">Solo</button>`;
			html += `<button class="${state.blendMode === 'blend' ? 'active' : ''}" onclick="window._setObjectMode('${objectId}','blend')">Blend</button>`;
		}
		html += '</div>';

		// Clips within this object
		html += '<div class="object-clips">';
		state.clips.forEach((clip, localIdx) => {
			const globalIdx = state.clipIndices[localIdx];
			const info = animationInfos[globalIdx] || {};
			const srcType = info.sourceType || '';
			const isActiveClip = (localIdx === state.activeClipIndex);

			html += `<div class="clip-item${isActiveClip ? ' active' : ''}">`;
			html += `<div class="clip-name">${clip.name || 'Unnamed'}</div>`;
			html += `<div class="clip-meta">${(clip.duration / sceneTimeCodesPerSecond).toFixed(2)}s | ${clip.tracks.length} tracks`;
			if (srcType) html += ` | ${srcType}`;
			html += '</div>';
			html += '<div class="clip-buttons">';
			html += `<button class="btn-solo" onclick="window._soloClipForObject('${objectId}',${localIdx})">Solo</button>`;
			if (state.clips.length > 1) {
				html += `<button class="btn-crossfade" onclick="window._crossfadeForObject('${objectId}',${localIdx})">Xfade</button>`;
			}
			html += '</div>';
			html += '</div>';
		});

		// Blend weights (visible in blend mode for this object)
		if (state.blendMode === 'blend') {
			state.clips.forEach((clip, localIdx) => {
				const action = state.actions[localIdx];
				const weight = action ? action.getEffectiveWeight() : 0;
				const name = (clip.name || 'Unnamed').substring(0, 12);
				html += '<div class="blend-row">';
				html += `<label title="${clip.name}">${name}</label>`;
				html += `<input type="range" min="0" max="1" step="0.05" value="${weight.toFixed(2)}" oninput="window._setObjectWeight('${objectId}',${localIdx},this.value)">`;
				html += `<span class="weight-val">${weight.toFixed(2)}</span>`;
				html += '</div>';
			});
		}

		html += '</div>'; // .object-clips
		html += '</div>'; // .object-group
	}

	// Global controls
	html += '<div class="global-controls">';
	html += `<button onclick="window._playAll()">Play All</button>`;
	html += `<button onclick="window._pauseAll()">Pause All</button>`;
	html += `<span style="font-size:10px;color:#aaa;">Speed:</span>`;
	html += `<input type="range" min="0.1" max="3.0" step="0.1" value="${playbackSpeed}" id="speed-slider" oninput="window._setSpeed(this.value)">`;
	html += `<span style="font-size:10px;color:#aaa;font-family:monospace;" id="speed-val">${playbackSpeed.toFixed(1)}x</span>`;
	html += '</div>';

	content.innerHTML = html;

	// Attach crossfade slider listener
	const cfSlider = document.getElementById('crossfade-slider');
	if (cfSlider) {
		cfSlider.addEventListener('input', (e) => {
			crossfadeDuration = parseFloat(e.target.value);
			const valEl = document.getElementById('crossfade-val');
			if (valEl) valEl.textContent = crossfadeDuration.toFixed(1) + 's';
		});
	}
}

// =====================================================
// UI Handlers — Per-Object Controls
// =====================================================

window._setFilter = function(mode) {
	filterMode = mode;
	renderClipPanel();
};

window._selectObject = function(objectId) {
	if (selectedObjectId === objectId) {
		deselectObject();
	} else {
		selectObject(objectId);
	}
};

window._togglePlayObject = function(objectId) {
	const state = objectAnimMap.get(objectId);
	if (!state) return;
	state.isPlaying = !state.isPlaying;
	for (const action of state.actions) {
		action.paused = !state.isPlaying;
	}
	renderClipPanel();
};

window._setObjectMode = function(objectId, mode) {
	const state = objectAnimMap.get(objectId);
	if (!state) return;
	state.blendMode = mode;
	if (mode === 'solo') {
		// Solo the active clip
		for (let i = 0; i < state.actions.length; i++) {
			state.actions[i].enabled = true;
			state.actions[i].setEffectiveWeight(i === state.activeClipIndex ? 1 : 0);
			if (i === state.activeClipIndex) state.actions[i].play();
		}
	} else {
		// Blend mode — all clips at full weight (additive blend)
		for (const action of state.actions) {
			action.enabled = true;
			action.setEffectiveWeight(1.0);
			action.play();
		}
	}
	renderClipPanel();
};

window._soloClipForObject = function(objectId, localIndex) {
	const state = objectAnimMap.get(objectId);
	if (!state) return;
	state.activeClipIndex = localIndex;
	state.blendMode = 'solo';
	for (let i = 0; i < state.actions.length; i++) {
		state.actions[i].enabled = true;
		state.actions[i].setEffectiveWeight(i === localIndex ? 1 : 0);
		if (i === localIndex) state.actions[i].play();
	}
	renderClipPanel();
};

window._crossfadeForObject = function(objectId, localIndex) {
	const state = objectAnimMap.get(objectId);
	if (!state) return;
	if (localIndex === state.activeClipIndex) return;
	// crossfadeDuration is in real seconds; mixer time is in frame-time, so scale it
	crossfadeActions(state.actions[state.activeClipIndex], state.actions[localIndex], crossfadeDuration * sceneTimeCodesPerSecond);
	state.activeClipIndex = localIndex;
	state.blendMode = 'solo';
	renderClipPanel();
};

window._setObjectWeight = function(objectId, localIndex, value) {
	const state = objectAnimMap.get(objectId);
	if (!state) return;
	const w = parseFloat(value);
	const action = state.actions[localIndex];
	if (action) {
		action.enabled = true;
		action.setEffectiveWeight(w);
		if (w > 0) action.play();
	}
};

window._playAll = function() {
	for (const [, state] of objectAnimMap) {
		state.isPlaying = true;
		for (const action of state.actions) {
			action.paused = false;
		}
	}
	isPlaying = true;
	if (mixer) mixer.timeScale = sceneTimeCodesPerSecond * playbackSpeed;
	renderClipPanel();
};

window._pauseAll = function() {
	for (const [, state] of objectAnimMap) {
		state.isPlaying = false;
		for (const action of state.actions) {
			action.paused = true;
		}
	}
	isPlaying = false;
	renderClipPanel();
};

window._setSpeed = function(value) {
	playbackSpeed = parseFloat(value);
	if (mixer) {
		mixer.timeScale = sceneTimeCodesPerSecond * playbackSpeed;
	}
	const valEl = document.getElementById('speed-val');
	if (valEl) valEl.textContent = playbackSpeed.toFixed(1) + 'x';
};

// =====================================================
// Animation Loop
// =====================================================

function animate() {
	requestAnimationFrame(animate);

	const delta = clock.getDelta();
	if (mixer) {
		mixer.update(delta);
	}

	updateSelectionHighlight();
	controls.update();
	renderer.render(scene, camera);
}

// =====================================================
// File Loading
// =====================================================

let loader = null;

async function initLoader() {
	loader = await createConfiguredTinyUSDZLoader();
	console.log('TinyUSDZ loader initialized');
}

async function loadFromURL(url) {
	if (!loader) await initLoader();
	try {
		const usdScene = await loadUSDSceneFromURL(loader, url);
		await processUSDScene(usdScene, url);
	} catch (err) {
		console.error('Failed to load USD file from URL:', err);
		const currentFileEl = document.getElementById('currentFile');
		if (currentFileEl) currentFileEl.textContent = 'Error: ' + err.message;
	}
}

async function loadFromArrayBuffer(buffer, filename) {
	if (!loader) await initLoader();
	try {
		const usdScene = await parseUSDSceneFromArrayBuffer(loader, buffer, filename);
		await processUSDScene(usdScene, filename);
	} catch (err) {
		console.error('Failed to parse USD file:', err);
		const currentFileEl = document.getElementById('currentFile');
		if (currentFileEl) currentFileEl.textContent = 'Error: ' + err.message;
	}
}

// Handle file upload events
window.addEventListener('loadUSDFile', async (event) => {
	const file = event.detail.file;
	if (!file) return;

	const buffer = await file.arrayBuffer();
	await loadFromArrayBuffer(buffer, file.name);
});

// Handle window resize
window.addEventListener('resize', () => {
	camera.aspect = window.innerWidth / window.innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(window.innerWidth, window.innerHeight);
});

// =====================================================
// Startup
// =====================================================

animate();

// Load default test asset
(async () => {
	const USD_URL = './assets/multi-clip-skeleton.usda';
	//const USD_URL = './assets/anim-clips.usdc';
	await loadFromURL(USD_URL);
})();
