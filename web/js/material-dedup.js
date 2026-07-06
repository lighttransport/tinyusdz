// TinyUSDZ Material / Mesh Dedup demo.
//
// Demonstrates the Tydra render-scene optimizations exposed through the WASM
// binding: material/texture deduplication, mesh merging (with transform
// baking) and render-tree flattening. Each toggle re-runs the native
// conversion (TinyUSDZLoader.parse with the corresponding options) and rebuilds
// the Three.js scene so the mesh / material / draw-call reduction is visible
// live.
//
// Also shows global scene scaling that honors the USD `metersPerUnit` stage
// metadata and up-axis (Z-up -> Y-up) conversion.
//
// Textures are always decoded lazily on the JS side (the native conversion does
// no texture decode — see convertScene), and stream in after the untextured
// scene renders.
//
// Recommended combinations:
//   - General viewer (no per-object picking): native Material Dedup + Mesh
//     Merge (bake). Fewest materials and draw calls; geometry is concatenated
//     so per-instance identity is lost. This is the default.
//   - Picking / selection needed: native Material Dedup + Mesh Merge OFF +
//     three.js "Batch By Material". BatchedMesh cuts draw calls while keeping
//     per-instance transforms and identity (raycast).
//   - "JS Material Dedup" is for the structural (textures-off) view only: it is
//     redundant with native material dedup, and with lazy textures it collapses
//     materials before their textures load (texture bleed). It is force-skipped
//     while textures are enabled (see applyJsPostProcess) and off by default.
//
// Patterned after animation.js / skin-anim.js / materialx.js / progress-demo.js.

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';

import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils, TextureLoadingManager } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { dedupMaterialsByContent, batchByMaterial } from 'tinyusdz/MeshBatching.js';

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

let renderer, scene, camera, controls, gui;
let usdSceneRoot; // group that holds the converted USD scene (scaled/oriented)
let loader = null; // reused TinyUSDZLoader instance
let currentUsd = null; // current native scene (embind object)

let rawBytes = null; // Uint8Array of the active model
let currentName = 'sample scene';

// metersPerUnit / upAxis from the active model's stage metadata.
let sceneMeta = { upAxis: 'Y', metersPerUnit: 1.0 };

// Baseline counts captured with all optimizations OFF, for comparison.
let baseline = null;

// Native render-scene counts of the current selection (so three.js-only
// post-process toggles can refresh stats without re-converting).
let currentCounts = null;

// Result of the last three.js post-process pass (material dedup / batching).
let jsPostStats = null;

// Background lazy texture loader for the currently displayed scene.
let textureManager = null;

const textureCache = new Map();
const frameState = {
	lastFpsUpdateMs: performance.now(),
	frameCount: 0
};

class NextTextureLoadingManager {
	constructor() {
		this.tasks = [];
		this.promiseCache = new Map();
		this.total = 0;
		this.loaded = 0;
		this.failed = 0;
		this.aborted = false;
	}

	queueTexture(material, mapProperty, adapter, assetPath, colorRole = 'data') {
		if (!assetPath) return;
		this.tasks.push({ material, mapProperty, adapter, assetPath, colorRole });
		this.total = this.tasks.length;
	}

	reset() {
		this.tasks = [];
		this.promiseCache.clear();
		this.total = 0;
		this.loaded = 0;
		this.failed = 0;
		this.aborted = false;
	}

	abort() {
		this.aborted = true;
	}

	async startLoading({ concurrency = 4, yieldInterval = 16, onTextureLoaded = null, onProgress = null } = {}) {
		this.aborted = false;
		let nextIndex = 0;
		const worker = async () => {
			while (!this.aborted && nextIndex < this.tasks.length) {
				const task = this.tasks[nextIndex++];
				try {
					const texture = await this.loadTexture(task);
					if (texture && !this.aborted) {
						task.material[task.mapProperty] = texture;
						task.material.needsUpdate = true;
						if (onTextureLoaded) onTextureLoaded(task.material, texture, task);
					}
					this.loaded++;
				} catch (error) {
					this.failed++;
					console.warn('[material-dedup] next texture load failed', {
						assetPath: task.assetPath,
						mapProperty: task.mapProperty,
						error: error?.message || String(error)
					});
				}
				if (onProgress) {
					onProgress({ loaded: this.loaded, failed: this.failed, total: this.total });
				}
				await new Promise((resolve) => setTimeout(resolve, yieldInterval));
			}
		};
		await Promise.all(Array.from({ length: Math.max(1, concurrency) }, worker));
	}

	async loadTexture(task) {
		const role = task.colorRole === 'color' ? 'color' : 'data';
		const key = `${role}:${task.assetPath}`;
		if (!this.promiseCache.has(key)) {
			this.promiseCache.set(key, loadNextArchiveTexture(task.adapter, task.assetPath, role));
		}
		return this.promiseCache.get(key);
	}
}

function getStartupUSDModelURI(params = new URLSearchParams(window.location.search)) {
	for (const key of ['uri', 'url', 'src', 'model', 'usd']) {
		const value = params.get(key);
		if (value) return value;
	}
	return null;
}

function getDisplayNameFromURI(uri) {
	try {
		const parsed = new URL(uri, window.location.href);
		return parsed.pathname.split('/').filter(Boolean).pop() || uri;
	} catch {
		return uri.split('/').pop() || uri;
	}
}

function formatDurationMs(ms) {
	if (!Number.isFinite(ms)) return 'n/a';
	return ms < 1000 ? `${ms.toFixed(0)} ms` : `${(ms / 1000).toFixed(2)} s`;
}

function formatBytes(bytes) {
	if (!Number.isFinite(bytes)) return 'n/a';
	const sign = bytes < 0 ? '-' : '';
	const units = ['B', 'KB', 'MB', 'GB'];
	let value = Math.abs(bytes);
	let unitIndex = 0;
	while (value >= 1024 && unitIndex < units.length - 1) {
		value /= 1024;
		unitIndex++;
	}
	return `${sign}${value.toFixed(unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

function captureMemorySnapshot() {
	const jsHeap = performance.memory?.usedJSHeapSize;
	const wasmHeap = loader?.native?.HEAPU8?.buffer?.byteLength ||
		loader?.native_?.HEAPU8?.buffer?.byteLength;
	return {
		jsHeap: Number.isFinite(jsHeap) ? jsHeap : null,
		wasmHeap: Number.isFinite(wasmHeap) ? wasmHeap : null
	};
}

function formatMemoryUse(before, after, key) {
	const current = after?.[key];
	if (!Number.isFinite(current)) return 'n/a';
	const previous = before?.[key];
	if (!Number.isFinite(previous)) return formatBytes(current);
	const delta = current - previous;
	return `${formatBytes(current)} (${delta > 0 ? '+' : ''}${formatBytes(delta)})`;
}

function beginLoadStats(fileSize = null) {
	const stats = {
		startTime: performance.now(),
		fileSize,
		fetchMs: null,
		parseMs: null,
		processMs: null,
		totalMs: null,
		memoryBefore: captureMemorySnapshot(),
		memoryAfter: null
	};
	updateLoadStatsPanel(stats, 'Loading...');
	return stats;
}

function updateLoadStatsPanel(stats, overrideText = null) {
	const el = document.getElementById('loadStats');
	if (!el) return;
	el.style.display = 'block';
	if (overrideText) {
		el.textContent = overrideText;
		return;
	}
	el.textContent = [
		`File: ${formatBytes(stats.fileSize)}`,
		`Fetch/read: ${stats.fetchMs === null ? 'n/a' : formatDurationMs(stats.fetchMs)}`,
		`Parse/load: ${formatDurationMs(stats.parseMs)}`,
		`Process/build: ${formatDurationMs(stats.processMs)}`,
		`Total: ${formatDurationMs(stats.totalMs)}`,
		`JS heap: ${formatMemoryUse(stats.memoryBefore, stats.memoryAfter, 'jsHeap')}`,
		`WASM heap: ${formatMemoryUse(stats.memoryBefore, stats.memoryAfter, 'wasmHeap')}`
	].join('\n');
}

function finishLoadStats(stats) {
	if (!stats) return;
	stats.totalMs = performance.now() - stats.startTime;
	stats.memoryAfter = captureMemorySnapshot();
	updateLoadStatsPanel(stats);
}

function failLoadStats(stats) {
	if (!stats) return;
	stats.totalMs = performance.now() - stats.startTime;
	stats.memoryAfter = captureMemorySnapshot();
	updateLoadStatsPanel(stats, 'Failed');
}

// Optimization + scene-transform parameters (driven by lil-gui).
const params = {
	backend: 'legacy',

	// Tydra render-scene optimizations.
	materialDedup: true,
	mergeMeshes: true,
	mergeMeshesBakeTransform: true,
	flattenRenderTree: false,

	// Scene transform.
	globalScale: 1.0,
	honorMetersPerUnit: true,
	upAxisConversion: true,

	// Render every material double-sided (debug aid for spotting inverted
	// winding / missing backfaces).
	doubleSided: false,

	// Apply textures, decoded lazily on the JS side (raw bytes are pulled from
	// the asset on demand by getImageCopy; the scene renders untextured first,
	// then textures stream in). On by default.
	loadTextures: true,

	// three.js-side post-process (applied after the scene is built, no
	// re-conversion). Independent of the native Tydra optimizations above.
	jsMaterialDedup: false,
	jsBatchByMaterial: false,

	// Sample scene generator.
	sampleGrid: 6,

	// Actions.
	resetOptimizations: () => {
		params.materialDedup = false;
		params.mergeMeshes = false;
		params.mergeMeshesBakeTransform = true;
		params.flattenRenderTree = false;
		refreshGuiControllers();
		rebuild();
	},
	enableAllOptimizations: () => {
		params.materialDedup = true;
		params.mergeMeshes = true;
		params.mergeMeshesBakeTransform = true;
		params.flattenRenderTree = true;
		refreshGuiControllers();
		rebuild();
	}
};

const guiControllers = [];

// ---------------------------------------------------------------------------
// Sample scene generation
// ---------------------------------------------------------------------------

// Build a USDA scene: a gridN x gridN grid of unit cubes. Each cube gets its
// own Material prim, but the parameters are drawn from a small palette, so many
// materials are byte-identical duplicates -> great fodder for material dedup,
// and cubes sharing a (deduped) material -> great fodder for mesh merge.
function buildSampleUSDA(gridN) {
	const palette = [
		[0.85, 0.25, 0.2],
		[0.2, 0.6, 0.85],
		[0.3, 0.8, 0.35],
		[0.9, 0.75, 0.2]
	];
	const h = 0.4; // half-extent
	const spacing = 1.4;

	const cubePoints = [
		[-h, -h, -h], [h, -h, -h], [h, h, -h], [-h, h, -h],
		[-h, -h, h], [h, -h, h], [h, h, h], [-h, h, h]
	];
	const faceCounts = [4, 4, 4, 4, 4, 4];
	const faceIndices = [
		0, 1, 2, 3,
		4, 7, 6, 5,
		0, 4, 5, 1,
		1, 5, 6, 2,
		2, 6, 7, 3,
		3, 7, 4, 0
	];

	const pointsStr = cubePoints.map((p) => `(${p[0]}, ${p[1]}, ${p[2]})`).join(', ');
	const fcStr = faceCounts.join(', ');
	const fiStr = faceIndices.join(', ');

	const lines = [];
	lines.push('#usda 1.0');
	lines.push('(');
	lines.push('    upAxis = "Y"');
	lines.push('    metersPerUnit = 1.0');
	lines.push('    doc = "Generated dedup demo scene"');
	lines.push(')');
	lines.push('');
	lines.push('def Xform "Root"');
	lines.push('{');

	const half = (gridN - 1) / 2;
	let idx = 0;
	for (let r = 0; r < gridN; r++) {
		for (let c = 0; c < gridN; c++) {
			const pal = palette[idx % palette.length];
			const tx = (c - half) * spacing;
			const tz = (r - half) * spacing;
			const matName = `Mat_${idx}`;
			const meshName = `Cube_${idx}`;

			// One Material prim per cube; only `palette.length` distinct
			// parameter sets, so material dedup collapses them.
			lines.push(`    def Material "${matName}"`);
			lines.push('    {');
			lines.push(`        token outputs:surface.connect = </Root/${matName}/Surface.outputs:surface>`);
			lines.push('        def Shader "Surface"');
			lines.push('        {');
			lines.push('            uniform token info:id = "UsdPreviewSurface"');
			lines.push(`            color3f inputs:diffuseColor = (${pal[0]}, ${pal[1]}, ${pal[2]})`);
			lines.push('            float inputs:metallic = 0.0');
			lines.push('            float inputs:roughness = 0.55');
			lines.push('            token outputs:surface');
			lines.push('        }');
			lines.push('    }');
			lines.push('');

			lines.push(`    def Mesh "${meshName}"`);
			lines.push('    {');
			lines.push(`        rel material:binding = </Root/${matName}>`);
			lines.push(`        point3f[] points = [${pointsStr}]`);
			lines.push(`        int[] faceVertexCounts = [${fcStr}]`);
			lines.push(`        int[] faceVertexIndices = [${fiStr}]`);
			lines.push(`        double3 xformOp:translate = (${tx}, 0, ${tz})`);
			lines.push('        uniform token[] xformOpOrder = ["xformOp:translate"]');
			lines.push('    }');
			lines.push('');
			idx++;
		}
	}

	lines.push('}');
	lines.push('');
	return lines.join('\n');
}

// ---------------------------------------------------------------------------
// Three.js setup
// ---------------------------------------------------------------------------

function initThree() {
	scene = new THREE.Scene();
	scene.background = new THREE.Color(0x202428);

	camera = new THREE.PerspectiveCamera(
		50, window.innerWidth / window.innerHeight, 0.01, 5000);
	camera.position.set(8, 7, 10);

	renderer = new THREE.WebGLRenderer({ antialias: true });
	renderer.setPixelRatio(window.devicePixelRatio);
	renderer.setSize(window.innerWidth, window.innerHeight);
	renderer.outputColorSpace = THREE.SRGBColorSpace;
	document.body.appendChild(renderer.domElement);

	controls = new OrbitControls(camera, renderer.domElement);
	controls.enableDamping = true;
	controls.target.set(0, 0, 0);

	// Lighting (no HDR needed for this structural demo).
	const hemi = new THREE.HemisphereLight(0xffffff, 0x444455, 1.1);
	scene.add(hemi);
	const dir = new THREE.DirectionalLight(0xffffff, 1.4);
	dir.position.set(5, 10, 7);
	scene.add(dir);
	const dir2 = new THREE.DirectionalLight(0x88aaff, 0.5);
	dir2.position.set(-6, 4, -5);
	scene.add(dir2);

	const grid = new THREE.GridHelper(40, 40, 0x556677, 0x334455);
	grid.position.y = -0.5;
	scene.add(grid);

	usdSceneRoot = new THREE.Group();
	scene.add(usdSceneRoot);

	window.addEventListener('resize', onResize);
	animate();
}

function onResize() {
	camera.aspect = window.innerWidth / window.innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(window.innerWidth, window.innerHeight);
}

function animate() {
	requestAnimationFrame(animate);
	frameState.frameCount++;
	const now = performance.now();
	if (now - frameState.lastFpsUpdateMs >= 500) {
		const fps = frameState.frameCount * 1000 / (now - frameState.lastFpsUpdateMs);
		const fpsEl = document.getElementById('fpsValue');
		if (fpsEl) fpsEl.textContent = fps.toFixed(1);
		frameState.frameCount = 0;
		frameState.lastFpsUpdateMs = now;
	}
	controls.update();
	renderer.render(scene, camera);
}

// ---------------------------------------------------------------------------
// Conversion + scene build
// ---------------------------------------------------------------------------

async function ensureLoader() {
	if (loader) return loader;
	loader = new TinyUSDZLoader();
	await loader.init({ useZstdCompressedWasm: false, useMemory64: false });
	return loader;
}

function parseWithOptions(bytes, name, options) {
	return new Promise((resolve, reject) => {
		loader.parse(bytes, name, resolve, reject, options);
	});
}

function countsFromUsd(usd) {
	return {
		meshes: usd.numMeshes ? usd.numMeshes() : 0,
		materials: usd.numMaterials ? usd.numMaterials() : 0,
		textures: usd.numTextures ? usd.numTextures() : 0
	};
}

function isNextScene(usd) {
	return usd && usd.__backend === 'next';
}

function textureColorRoleForMap(mapProperty) {
	return (mapProperty === 'map' || mapProperty === 'emissiveMap') ? 'color' : 'data';
}

async function loadNextArchiveTexture(adapter, assetPath, role = 'data') {
	const bytes = adapter.getArchiveTextureBytes(assetPath);
	if (!bytes) {
		throw new Error(`texture asset not found in archive: ${assetPath}`);
	}
	const blob = new Blob([bytes.slice ? bytes.slice() : bytes]);
	const blobUrl = URL.createObjectURL(blob);
	try {
		const texture = await new THREE.TextureLoader().loadAsync(blobUrl);
		texture.colorSpace = role === 'color' ? THREE.SRGBColorSpace : THREE.NoColorSpace;
		texture.wrapS = THREE.RepeatWrapping;
		texture.wrapT = THREE.RepeatWrapping;
		texture.flipY = true;
		texture.needsUpdate = true;
		return texture;
	} finally {
		URL.revokeObjectURL(blobUrl);
	}
}

function createNextMaterial(mesh, adapter, textureManager, skipTextures) {
	const src = mesh.material || {};
	const paths = mesh.texturePaths || {};
	const hasBaseMap = !!paths.baseColor && !skipTextures;
	const material = new THREE.MeshPhysicalMaterial({
		color: hasBaseMap ? new THREE.Color(1, 1, 1) :
			new THREE.Color(src.baseColor?.[0] ?? 0.8, src.baseColor?.[1] ?? 0.8, src.baseColor?.[2] ?? 0.8),
		metalness: src.metallic ?? 0,
		roughness: src.roughness ?? 0.5,
		emissive: new THREE.Color(src.emissive?.[0] ?? 0, src.emissive?.[1] ?? 0, src.emissive?.[2] ?? 0),
		transparent: (src.opacity ?? 1) < 1,
		opacity: src.opacity ?? 1,
		alphaTest: (src.opacityThreshold ?? -1) > 0 ? src.opacityThreshold : 0
	});
	material.userData.nextTexturePaths = paths;

	const queue = (mapProperty, assetPath) => {
		if (!assetPath || skipTextures || !textureManager) return;
		textureManager.queueTexture(
			material, mapProperty, adapter, assetPath, textureColorRoleForMap(mapProperty));
	};
	queue('map', paths.baseColor);
	queue('normalMap', paths.normal);
	queue('roughnessMap', paths.roughness);
	queue('metalnessMap', paths.metallic);
	queue('aoMap', paths.occlusion);
	queue('emissiveMap', paths.emissive);
	return material;
}

function buildNextThreeNode(adapter, { skipTextures = true, lazyTextures = false } = {}) {
	const group = new THREE.Group();
	group.name = adapter.filename || 'next-scene';
	const manager = (lazyTextures && !skipTextures) ? new NextTextureLoadingManager() : null;
	const applyUsdRowMajorMatrix = (object, matrix) => {
		if (!Array.isArray(matrix) || matrix.length !== 16) return;
		object.matrix.set(
			matrix[0], matrix[4], matrix[8], matrix[12],
			matrix[1], matrix[5], matrix[9], matrix[13],
			matrix[2], matrix[6], matrix[10], matrix[14],
			matrix[3], matrix[7], matrix[11], matrix[15]
		);
		object.matrixAutoUpdate = false;
	};

	for (const mesh of adapter.meshes || []) {
		if (!mesh.points || mesh.points.length === 0) continue;
		const geometry = new THREE.BufferGeometry();
		geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));
		if (mesh.normals && mesh.normals.length) {
			geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
		} else {
			geometry.computeVertexNormals();
		}
		if (mesh.uv0 && mesh.uv0.length) {
			geometry.setAttribute('uv', new THREE.BufferAttribute(mesh.uv0, 2));
			geometry.setAttribute('uv2', new THREE.BufferAttribute(mesh.uv0, 2));
		}
		if (mesh.indices && mesh.indices.length) {
			geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1));
		}
		geometry.computeBoundingSphere();
		let material = createNextMaterial(mesh, adapter, manager, skipTextures);
		if (Array.isArray(mesh.materials) && mesh.materials.length && Array.isArray(mesh.submeshes) && mesh.submeshes.length) {
			const materialEntries = mesh.materials.map((entry) => ({
				material: entry.material || mesh.material,
				texturePaths: entry.texturePaths || {},
				materialKey: entry.materialKey || mesh.materialKey
			}));
			materialEntries.push({
				material: mesh.material,
				texturePaths: mesh.texturePaths,
				materialKey: mesh.materialKey
			});
			const fallbackIndex = materialEntries.length - 1;
			material = materialEntries.map((entry) => createNextMaterial(entry, adapter, manager, skipTextures));
			const covered = [];
			for (const group of mesh.submeshes) {
				const start = Math.max(0, group.start | 0);
				const count = Math.max(0, group.count | 0);
				if (count <= 0) continue;
				const materialIndex = (group.materialIndex >= 0 && group.materialIndex < material.length)
					? group.materialIndex : fallbackIndex;
				geometry.addGroup(start, count, materialIndex);
				covered.push([start, start + count]);
			}
			const total = mesh.indices && mesh.indices.length ? mesh.indices.length : mesh.points.length / 3;
			covered.sort((a, b) => a[0] - b[0]);
			let cursor = 0;
			for (const [start, end] of covered) {
				if (start > cursor) geometry.addGroup(cursor, start - cursor, fallbackIndex);
				cursor = Math.max(cursor, end);
			}
			if (cursor < total) geometry.addGroup(cursor, total - cursor, fallbackIndex);
		}
		const threeMesh = new THREE.Mesh(geometry, material);
		threeMesh.name = mesh.primPath || mesh.primName || `mesh_${mesh.index}`;
		applyUsdRowMajorMatrix(threeMesh, mesh.worldMatrix);
		group.add(threeMesh);
	}

	return { node: group, textureManager: manager };
}

// Read scene metadata (upAxis / metersPerUnit) defensively.
function readSceneMeta(usd) {
	const md = (usd.getSceneMetadata) ? usd.getSceneMetadata() : {};
	return {
		upAxis: md.upAxis || 'Y',
		metersPerUnit: (typeof md.metersPerUnit === 'number' && md.metersPerUnit > 0)
			? md.metersPerUnit : 1.0
	};
}

// Apply global scale (optionally folding in metersPerUnit) and up-axis fix.
function applySceneTransform() {
	let effectiveScale = params.globalScale;
	if (params.honorMetersPerUnit && sceneMeta.metersPerUnit) {
		effectiveScale *= sceneMeta.metersPerUnit;
	}
	usdSceneRoot.scale.setScalar(effectiveScale);

	usdSceneRoot.rotation.x =
		(params.upAxisConversion && sceneMeta.upAxis === 'Z') ? -Math.PI / 2 : 0;
}

// Debug: force every material in the scene to render front-only or double-sided.
function applyDoubleSided() {
	const side = params.doubleSided ? THREE.DoubleSide : THREE.FrontSide;
	usdSceneRoot.traverse((o) => {
		if (!o.isMesh || !o.material) return;
		const mats = Array.isArray(o.material) ? o.material : [o.material];
		for (const m of mats) {
			if (m && m.side !== side) {
				m.side = side;
				m.needsUpdate = true;
			}
		}
	});
}

// Fit the camera to the current USD scene bounds so loaded models (which may
// be authored at any scale) are framed regardless of metersPerUnit.
//
// Robust to outliers: scenes often contain a huge sky dome or ground plane
// whose bounds dwarf the actual content. We compute per-mesh world bounds,
// drop meshes far larger than the median, and frame the remainder.
function frameCameraToScene() {
	usdSceneRoot.updateMatrixWorld(true);

	const boxes = [];
	usdSceneRoot.traverse((o) => {
		if (!o.isMesh || !o.geometry) return;
		if (!o.geometry.boundingBox) o.geometry.computeBoundingBox();
		if (!o.geometry.boundingBox) return;
		const b = o.geometry.boundingBox.clone().applyMatrix4(o.matrixWorld);
		if (!b.isEmpty()) boxes.push(b);
	});

	let box = new THREE.Box3();
	if (boxes.length === 0) {
		box.setFromObject(usdSceneRoot);
	} else {
		// Median per-mesh diagonal; exclude meshes >8x that (sky dome/ground).
		const diags = boxes
			.map((b) => b.getSize(new THREE.Vector3()).length())
			.sort((a, b) => a - b);
		const median = diags[Math.floor(diags.length / 2)] || 0;
		const limit = median * 8;
		let used = 0;
		for (let i = 0; i < boxes.length; i++) {
			if (median > 0 && boxes[i].getSize(new THREE.Vector3()).length() > limit) {
				continue;
			}
			box.union(boxes[i]);
			used++;
		}
		if (used === 0 || box.isEmpty()) {
			box = new THREE.Box3().setFromObject(usdSceneRoot);
		}
	}
	if (box.isEmpty()) return;

	const center = new THREE.Vector3();
	const size = new THREE.Vector3();
	box.getCenter(center);
	box.getSize(size);
	const radius = Math.max(size.x, size.y, size.z) || 1;
	camera.position.set(center.x + radius * 0.8, center.y + radius * 0.6,
		center.z + radius * 1.1);
	camera.near = Math.max(radius / 1000, 1e-4);
	camera.far = radius * 20;
	camera.updateProjectionMatrix();
	controls.target.copy(center);
	controls.update();
}

function collectMaterialTextures(material, textures) {
	if (!material) return;
	for (const key of Object.keys(material)) {
		const value = material[key];
		if (value && value.isTexture) {
			textures.add(value);
		}
	}
	if (material.uniforms) {
		for (const uniform of Object.values(material.uniforms)) {
			const value = uniform?.value;
			if (value && value.isTexture) {
				textures.add(value);
			}
		}
	}
}

function disposeMaterial(material) {
	if (!material) return;
	const textures = new Set();
	collectMaterialTextures(material, textures);
	for (const texture of textures) {
		if (texture.image && typeof texture.image.close === 'function') {
			try { texture.image.close(); } catch (e) { /* ignore */ }
		}
		texture.dispose();
	}
	material.dispose();
}

function disposeTextureCache() {
	for (const value of textureCache.values()) {
		if (value && value.isTexture) {
			if (value.image && typeof value.image.close === 'function') {
				try { value.image.close(); } catch (e) { /* ignore */ }
			}
			value.dispose();
		}
	}
	textureCache.clear();
}

function disposeSceneRoot() {
	for (let i = usdSceneRoot.children.length - 1; i >= 0; i--) {
		const child = usdSceneRoot.children[i];
		usdSceneRoot.remove(child);
		child.traverse((o) => {
			// BatchedMesh owns internal GPU buffers; dispose() releases them.
			if (o.isBatchedMesh && typeof o.dispose === 'function') o.dispose();
			if (o.geometry) o.geometry.dispose();
			if (o.material) {
				const mats = Array.isArray(o.material) ? o.material : [o.material];
				mats.forEach(disposeMaterial);
			}
		});
	}
}

function freeUsd(usd) {
	if (usd && typeof usd.delete === 'function') {
		try { usd.delete(); } catch (e) { /* ignore */ }
	}
}

function releaseCurrentUSDResources({ clearRawBytes = false } = {}) {
	abortTextureManager();
	disposeSceneRoot();
	disposeTextureCache();
	freeUsd(currentUsd);
	currentUsd = null;
	currentCounts = null;
	jsPostStats = null;
	if (clearRawBytes) {
		rawBytes = null;
	}
}

// Convert `bytes` with the given optimization options and return the native
// scene + counts. Does NOT mount it into the Three.js scene.
async function convertScene(bytes, name, opts) {
	await ensureLoader();
	const usd = await parseWithOptions(bytes, name, {
		backend: opts.backend || params.backend || 'legacy',
		materialDedup: !!opts.materialDedup,
		mergeMeshes: !!opts.mergeMeshes,
		mergeMeshesBakeTransform: !!opts.mergeMeshesBakeTransform,
		flattenRenderTree: !!opts.flattenRenderTree,
		// Never decode textures during native conversion: that bulk-decodes the
		// whole (undeduplicated) image set into the wasm heap and OOMs texture-
		// heavy scenes on wasm32. Instead the JS layer pulls raw bytes per image
		// on demand (getImageCopy → ensureImageBufferLoaded_) and decodes them
		// lazily via TextureLoadingManager.
		loadTextureInNative: false
	});
	return { usd, counts: countsFromUsd(usd) };
}

// Apply the enabled three.js-side post-process passes to a freshly built node.
// Material dedup runs first so batchByMaterial can group by material identity.
function applyJsPostProcess(node) {
	jsPostStats = null;
	let dedup = null;
	let batch = null;
	// JS material dedup keys on material content INCLUDING texture refs. With
	// lazy textures the maps aren't loaded yet, so it would over-merge materials
	// that differ only by texture and orphan their queued texture loads. Skip it
	// whenever textures are enabled (native material dedup already covers it).
	if (params.jsMaterialDedup && !params.loadTextures) {
		dedup = dedupMaterialsByContent(node);
	}
	if (params.jsBatchByMaterial) {
		batch = batchByMaterial(node);
	}
	if (dedup || batch) {
		jsPostStats = { dedup, batch };
	}
}

// Build a Three.js node from a native scene and mount it under usdSceneRoot.
// `postProcess` enables the three.js-side optimization passes (only meaningful
// for the displayed/current scene, not the transient baseline mount).
// `lazyTextures` queues textures for background decoding via a returned
// TextureLoadingManager (the caller starts loading after the first render).
async function mountScene(usd,
		{ skipTextures = true, postProcess = false, lazyTextures = false } = {}) {
	abortTextureManager();
	disposeSceneRoot();
	disposeTextureCache();

	if (isNextScene(usd)) {
		const built = buildNextThreeNode(usd, { skipTextures, lazyTextures });
		if (postProcess) {
			applyJsPostProcess(built.node);
		} else {
			jsPostStats = null;
		}
		usdSceneRoot.add(built.node);
		applySceneTransform();
		applyDoubleSided();
		textureManager = built.textureManager;
		updateDebugHandle();
		return;
	}

	const usdRootNode = usd.getDefaultRootNode();
	const defaultMtl = new THREE.MeshStandardMaterial({
		color: 0x888888, roughness: 0.6, metalness: 0.0
	});
	// Lazy texture mode: queue textures instead of loading them inline, so the
	// scene appears immediately and textures stream in afterwards.
	const manager = (lazyTextures && !skipTextures) ? new TextureLoadingManager() : null;

	const threeNode = await TinyUSDZLoaderUtils.buildThreeNode(
		usdRootNode, defaultMtl, usd, {
			overrideMaterial: false,
			preferredMaterialType: 'auto',
			textureCache,
			storeMaterialData: true,
			// When textures are disabled this is a geometry/count-focused view;
			// skipping texture loads also avoids 404 spam from unresolved URIs.
			skipTextures,
			textureLoadingManager: manager
		});

	if (postProcess) {
		applyJsPostProcess(threeNode);
	} else {
		jsPostStats = null;
	}

	usdSceneRoot.add(threeNode);
	applySceneTransform();
	applyDoubleSided();
	textureManager = manager;
	updateDebugHandle();
}

function abortTextureManager() {
	if (textureManager) {
		try { textureManager.abort(); textureManager.reset(); } catch (e) { /* ignore */ }
		textureManager = null;
		updateDebugHandle();
	}
}

// Kick off background texture decoding/upload for the current scene (after the
// untextured scene is already visible). Loads a few at a time, yielding to the
// browser so the UI stays responsive.
function startLazyTextureLoading() {
	if (!textureManager || textureManager.total <= 0) return;
	const mgr = textureManager;
	updateDebugHandle();
	mgr.startLoading({
		concurrency: 4,
		yieldInterval: 16,
		onTextureLoaded: (material) => { material.needsUpdate = true; },
		onProgress: (info) => {
			setStatus(`${describeOptions()} · textures ${info.loaded}/${info.total}` +
				(info.failed ? ` (${info.failed} failed)` : ''));
		}
	}).then(() => setStatus(describeOptions()))
		.catch((err) => console.warn('[material-dedup] texture loading:', err));
}

// Deterministically measure the built USD scene by walking usdSceneRoot. This
// avoids relying on renderer.info (which only reflects the last drawn frame and
// undercounts on the first post-rebuild frame). `draws` counts renderable
// submeshes — one per mesh, or one per geometry group for multi-material
// meshes — which is the draw-call count Three.js will submit for the model.
function measureSceneInfo() {
	let draws = 0;
	let triangles = 0;
	usdSceneRoot.traverse((o) => {
		if (!o.isMesh || !o.geometry) return;
		if (o.isBatchedMesh) {
			// A BatchedMesh is a single multi-draw call covering all its
			// instances; its geometry already holds the combined index buffer.
			draws += 1;
			const idx = o.geometry.getIndex();
			if (idx) triangles += Math.floor(idx.count / 3);
			return;
		}
		const groups = o.geometry.groups ? o.geometry.groups.length : 0;
		draws += groups > 0 ? groups : 1;
		const idx = o.geometry.getIndex();
		const count = idx ? idx.count
			: (o.geometry.attributes.position ? o.geometry.attributes.position.count : 0);
		triangles += Math.floor(count / 3);
	});
	return { draws, triangles };
}

// Full (re)load of a model: capture the all-optimizations-OFF baseline, then
// build the scene with the currently selected options.
async function loadModel(bytes, name, stats = null) {
	window.renderComplete = false;
	window.renderError = null;
	updateDebugHandle();
	releaseCurrentUSDResources({ clearRawBytes: true });
	baseline = null;
	rawBytes = bytes;
	currentName = name;
	document.getElementById('currentFile').textContent = name;
	setStatus('Converting baseline (no optimizations)…');

	let baseResult = null;
	try {
		// Baseline: everything off (bake-transform is irrelevant when not merging).
		const parseStart = performance.now();
		baseResult = await convertScene(bytes, name, {
			materialDedup: false,
			mergeMeshes: false,
			mergeMeshesBakeTransform: false,
			flattenRenderTree: false
		});
		if (stats) stats.parseMs = performance.now() - parseStart;
		sceneMeta = readSceneMeta(baseResult.usd);
		// Baseline is for counts only; never decode/apply its (undeduplicated)
		// texture set — it can be enormous and is immediately replaced below.
		await mountScene(baseResult.usd, { skipTextures: true });
		const baseInfo = measureSceneInfo();
		baseline = {
			meshes: baseResult.counts.meshes,
			materials: baseResult.counts.materials,
			textures: baseResult.counts.textures,
			draws: baseInfo.draws,
			triangles: baseInfo.triangles
		};
		freeUsd(baseResult.usd);
		baseResult = null;

		// Now the current selection, then frame the camera once for this model.
		const processStart = performance.now();
		await rebuild();
		if (stats) stats.processMs = performance.now() - processStart;
		frameCameraToScene();
		finishLoadStats(stats);
		window.renderComplete = true;
		updateDebugHandle();
	} catch (err) {
		console.error(err);
		failLoadStats(stats);
		window.renderError = err && err.message ? err.message : String(err);
		updateDebugHandle();
		setStatus('Error: ' + (err && err.message ? err.message : String(err)));
		if (baseResult) {
			freeUsd(baseResult.usd);
		}
		releaseCurrentUSDResources({ clearRawBytes: true });
	}
}

// Re-convert + rebuild with the current optimization options (baseline kept).
async function rebuild() {
	if (!rawBytes) return;
	setStatus('Converting with current options…');
	// Stop texture loads, dispose Three.js/GPU objects, and free the previous
	// native scene before parsing the next variant. This keeps repeated reloads
	// and optimization toggles from retaining multiple large USD scenes at once.
	releaseCurrentUSDResources();
	let result = null;
	try {
		result = await convertScene(rawBytes, currentName, params);
		const usd = result.usd;
		const counts = result.counts;
		sceneMeta = readSceneMeta(usd);
		currentUsd = usd;
		result = null;
		currentCounts = counts;
		updateDebugHandle();
		await mountScene(usd, {
			skipTextures: !params.loadTextures, postProcess: true,
			lazyTextures: params.loadTextures });
		const info = measureSceneInfo();
		updateStatsUI(counts, info);
		setStatus(describeOptions());
		startLazyTextureLoading();
		updateDebugHandle();
	} catch (err) {
		if (result) {
			freeUsd(result.usd);
		}
		releaseCurrentUSDResources();
		console.error(err);
		setStatus('Error: ' + (err && err.message ? err.message : String(err)));
		updateDebugHandle();
	}
}

// Re-apply only the three.js post-process passes on the already-converted
// scene (no native re-conversion). Used when toggling the JS-side options.
async function reapplyThreePostProcess() {
	if (!currentUsd || !currentCounts) return;
	setStatus('Applying three.js post-process…');
	try {
		await mountScene(currentUsd, {
			skipTextures: !params.loadTextures, postProcess: true,
			lazyTextures: params.loadTextures });
		const info = measureSceneInfo();
		updateStatsUI(currentCounts, info);
		setStatus(describeOptions());
		startLazyTextureLoading();
	} catch (err) {
		console.error(err);
		setStatus('Error: ' + (err && err.message ? err.message : String(err)));
	}
}

function describeOptions() {
	const on = [];
	const backend = currentUsd?.__backend || params.backend || 'legacy';
	const backendNote = `backend ${backend}` +
		(currentUsd?.__backendFallbackReason ? ` (fallback: ${currentUsd.__backendFallbackReason})` : '');
	if (params.materialDedup) on.push('material-dedup');
	if (params.mergeMeshes) on.push(params.mergeMeshesBakeTransform ? 'merge-meshes(bake)' : 'merge-meshes');
	if (params.flattenRenderTree) on.push('flatten-tree');
	const scaleNote = params.honorMetersPerUnit && sceneMeta.metersPerUnit !== 1.0
		? `, scale ×${(params.globalScale * sceneMeta.metersPerUnit).toFixed(3)} (mpu ${sceneMeta.metersPerUnit})`
		: '';

	const js = [];
	if (jsPostStats && jsPostStats.dedup) {
		const d = jsPostStats.dedup;
		js.push(`js-mat-dedup ${d.uniqueMaterials} unique / ${d.replacedBindings} rebound`);
	}
	if (jsPostStats && jsPostStats.batch) {
		const b = jsPostStats.batch;
		js.push(`batched ${b.batchedMeshCount} meshes → ${b.batchCount} BatchedMesh`);
	}
	const jsNote = js.length ? ` · three.js: ${js.join(', ')}` : '';

	return `${backendNote} · ` + (on.length ? on.join(', ') : 'no optimizations') +
		` · upAxis ${sceneMeta.upAxis}${scaleNote}${jsNote}`;
}

// ---------------------------------------------------------------------------
// Stats UI
// ---------------------------------------------------------------------------

function setStatus(msg) {
	document.getElementById('status').textContent = msg;
}

function updateDebugHandle() {
	window.__materialDedupDebug = {
		scene,
		root: usdSceneRoot,
		textureManager,
		textureCache,
		currentUsd,
		currentCounts,
		params
	};
}

function deltaText(base, cur) {
	if (base == null || cur == null) return '';
	const d = cur - base;
	if (d === 0) return '±0';
	const pct = base > 0 ? Math.round((d / base) * 100) : 0;
	return `${d > 0 ? '+' : ''}${d} (${pct > 0 ? '+' : ''}${pct}%)`;
}

function setCell(id, value) {
	const el = document.getElementById(id);
	if (el) el.textContent = (value == null) ? '–' : String(value);
}

function updateStatsUI(counts, info) {
	const b = baseline || {};
	setCell('meshes-base', b.meshes);
	setCell('materials-base', b.materials);
	setCell('textures-base', b.textures);
	setCell('draws-base', b.draws);
	setCell('tris-base', b.triangles);

	setCell('meshes-cur', counts.meshes);
	setCell('materials-cur', counts.materials);
	setCell('textures-cur', counts.textures);
	setCell('draws-cur', info.draws);
	setCell('tris-cur', info.triangles);

	setCell('meshes-delta', deltaText(b.meshes, counts.meshes));
	setCell('materials-delta', deltaText(b.materials, counts.materials));
	setCell('textures-delta', deltaText(b.textures, counts.textures));
	setCell('draws-delta', deltaText(b.draws, info.draws));
	setCell('tris-delta', deltaText(b.triangles, info.triangles));
}

// ---------------------------------------------------------------------------
// GUI
// ---------------------------------------------------------------------------

function refreshGuiControllers() {
	guiControllers.forEach((c) => c.updateDisplay());
}

function buildGui() {
	gui = new GUI({ title: 'Dedup / Optimization' });

	const backendFolder = gui.addFolder('Backend');
	guiControllers.push(backendFolder.add(params, 'backend', ['legacy', 'next', 'auto'])
		.name('Loader Backend').onChange(rebuild));
	backendFolder.open();

	const optFolder = gui.addFolder('Tydra Optimizations');
	guiControllers.push(optFolder.add(params, 'materialDedup').name('Material Dedup').onChange(rebuild));
	guiControllers.push(optFolder.add(params, 'mergeMeshes').name('Merge Meshes').onChange(rebuild));
	guiControllers.push(optFolder.add(params, 'mergeMeshesBakeTransform').name('Bake Transform').onChange(rebuild));
	guiControllers.push(optFolder.add(params, 'flattenRenderTree').name('Flatten Render Tree').onChange(rebuild));
	optFolder.add(params, 'enableAllOptimizations').name('▶ Enable All');
	optFolder.add(params, 'resetOptimizations').name('■ Disable All');
	optFolder.open();

	const sceneFolder = gui.addFolder('Scene');
	sceneFolder.add(params, 'globalScale', 0.001, 100, 0.001).name('Global Scale').onChange(applySceneTransform);
	sceneFolder.add(params, 'honorMetersPerUnit').name('Honor metersPerUnit').onChange(applySceneTransform);
	sceneFolder.add(params, 'upAxisConversion').name('Z-up → Y-up').onChange(applySceneTransform);
	// Textures decode lazily on the JS side, so toggling only needs a re-mount
	// (no native re-conversion).
	guiControllers.push(sceneFolder.add(params, 'loadTextures').name('Load Textures').onChange(reapplyThreePostProcess));
	sceneFolder.add(params, 'doubleSided').name('Double-Sided (debug)').onChange(applyDoubleSided);
	sceneFolder.open();

	// three.js-side post-process: operates on the built scene graph, no native
	// re-conversion. Complementary to the Tydra optimizations above.
	const jsFolder = gui.addFolder('three.js Post-process');
	guiControllers.push(jsFolder.add(params, 'jsMaterialDedup')
		.name('JS Material Dedup').onChange(reapplyThreePostProcess));
	guiControllers.push(jsFolder.add(params, 'jsBatchByMaterial')
		.name('Batch By Material').onChange(reapplyThreePostProcess));
	jsFolder.open();

	const sampleFolder = gui.addFolder('Sample Scene');
	sampleFolder.add(params, 'sampleGrid', 2, 16, 1).name('Grid N×N');
	sampleFolder.add({ regen: loadSampleScene }, 'regen').name('Regenerate Sample');
}

// ---------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------

async function loadSampleScene() {
	const usda = buildSampleUSDA(Math.max(2, Math.round(params.sampleGrid)));
	const bytes = new TextEncoder().encode(usda);
	const stats = beginLoadStats(bytes.byteLength);
	stats.fetchMs = 0;
	await loadModel(bytes, `sample ${params.sampleGrid}×${params.sampleGrid} grid`, stats);
}

// Load a USD asset served over HTTP.
async function loadModelFromURL(url, stats = null) {
	setStatus('Fetching ' + url + ' …');
	const localStats = stats || beginLoadStats();
	const fetchStart = performance.now();
	const resp = await fetch(url);
	if (!resp.ok) throw new Error(`fetch ${url}: ${resp.status}`);
	const buf = await resp.arrayBuffer();
	localStats.fetchMs = performance.now() - fetchStart;
	localStats.fileSize = buf.byteLength;
	await loadModel(new Uint8Array(buf), getDisplayNameFromURI(url), localStats);
}

function setupFileInput() {
	document.getElementById('fileInput').addEventListener('change', async (event) => {
		const file = event.target.files[0];
		if (!file) return;
		const stats = beginLoadStats();
		try {
			const readStart = performance.now();
			const buf = await file.arrayBuffer();
			stats.fetchMs = performance.now() - readStart;
			stats.fileSize = buf.byteLength;
			await loadModel(new Uint8Array(buf), file.name, stats);
		} catch (err) {
			console.error(err);
			failLoadStats(stats);
			setStatus('Error: ' + (err && err.message ? err.message : String(err)));
		} finally {
			event.target.value = '';
		}
	});

	document.getElementById('loadSampleBtn').addEventListener('click', loadSampleScene);

	// GUI toggle button + 'H' shortcut.
	const toggleBtn = document.getElementById('gui-toggle');
	const toggleGui = () => {
		if (!gui) return;
		const el = gui.domElement;
		el.style.display = (el.style.display === 'none') ? '' : 'none';
	};
	toggleBtn.addEventListener('click', toggleGui);
	window.addEventListener('keydown', (e) => {
		if (e.key === 'h' || e.key === 'H') toggleGui();
	});
}

function setupDragAndDrop() {
	document.body.addEventListener('dragover', (event) => {
		event.preventDefault();
		document.body.classList.add('drag-over');
	});
	document.body.addEventListener('dragleave', (event) => {
		if (!event.relatedTarget || !document.body.contains(event.relatedTarget)) {
			document.body.classList.remove('drag-over');
		}
	});
	document.body.addEventListener('drop', async (event) => {
		event.preventDefault();
		document.body.classList.remove('drag-over');
		const file = event.dataTransfer?.files?.[0];
		if (!file) return;
		if (!/\.(usd|usda|usdc|usdz)$/i.test(file.name)) {
			setStatus('Please drop a USD file (.usd, .usda, .usdc, .usdz)');
			return;
		}
		const stats = beginLoadStats();
		try {
			const readStart = performance.now();
			const buf = await file.arrayBuffer();
			stats.fetchMs = performance.now() - readStart;
			stats.fileSize = buf.byteLength;
			await loadModel(new Uint8Array(buf), file.name, stats);
		} catch (err) {
			console.error(err);
			failLoadStats(stats);
			setStatus('Error: ' + (err && err.message ? err.message : String(err)));
		}
	});
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

async function main() {
	initThree();
	buildGui();
	setupFileInput();
	setupDragAndDrop();

	// Allow ?uri= / ?url= / ?src= / ?model= / ?usd= to load an asset directly
	// (handy for testing
	// large external models without the file picker). Optional flags configure
	// the initial state (avoids extra re-conversions when scripting tests):
	//   textures=1  dedup=0/1  merge=0/1  bake=0/1  flatten=0/1  js=1
	const search = new URLSearchParams(window.location.search);
	const flag = (key, cur) => {
		const v = search.get(key);
		return v == null ? cur : (v === '1' || v === 'true');
	};
	params.loadTextures = flag('textures', params.loadTextures);
	const backend = search.get('backend');
	if (backend === 'legacy' || backend === 'next' || backend === 'auto') {
		params.backend = backend;
	}
	params.materialDedup = flag('dedup', params.materialDedup);
	params.mergeMeshes = flag('merge', params.mergeMeshes);
	params.mergeMeshesBakeTransform = flag('bake', params.mergeMeshesBakeTransform);
	params.flattenRenderTree = flag('flatten', params.flattenRenderTree);
	if (search.get('js') === '1') {
		params.jsMaterialDedup = true;
		params.jsBatchByMaterial = true;
	}
	refreshGuiControllers();
	const urlParam = getStartupUSDModelURI(search);
	if (urlParam) {
		try {
			await loadModelFromURL(urlParam);
			return;
		} catch (err) {
			console.error(err);
			setStatus('Error loading URL: ' + (err && err.message ? err.message : String(err)));
		}
	}
	await loadSampleScene();
}

main();
