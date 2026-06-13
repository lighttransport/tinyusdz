import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';

import initTinyUSDZ from './src/tinyusdz/tinyusdz.js';

// ===========================================================================
// Scene Setup
// ===========================================================================

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1a1a2e);

const camera = new THREE.PerspectiveCamera(
	60, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.set(3, 3, 5);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.05;
controls.minDistance = 2;
controls.maxDistance = 20;

scene.add(new THREE.AmbientLight(0x404040, 0.5));
const directionalLight1 = new THREE.DirectionalLight(0xffffff, 0.8);
directionalLight1.position.set(5, 10, 7);
directionalLight1.castShadow = true;
scene.add(directionalLight1);
const directionalLight2 = new THREE.DirectionalLight(0x6bb6ff, 0.3);
directionalLight2.position.set(-5, 5, -5);
scene.add(directionalLight2);

scene.add(new THREE.GridHelper(10, 10, 0x444444, 0x222222));
scene.add(new THREE.AxesHelper(2));

// ===========================================================================
// Control meshes (typed arrays handed to the wasm subdivider)
// ===========================================================================

function makeControlMesh(vertices, faces) {
	const points = new Float32Array(vertices.length * 3);
	for (let i = 0; i < vertices.length; i++) {
		points[i * 3] = vertices[i][0];
		points[i * 3 + 1] = vertices[i][1];
		points[i * 3 + 2] = vertices[i][2];
	}
	const fvc = new Uint32Array(faces.length);
	let n = 0;
	for (const f of faces) n += f.length;
	const fvi = new Uint32Array(n);
	let k = 0;
	for (let i = 0; i < faces.length; i++) {
		fvc[i] = faces[i].length;
		for (const v of faces[i]) fvi[k++] = v;
	}
	return { points, fvc, fvi };
}

function cubeControlMesh() {
	return makeControlMesh(
		[[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
		 [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]],
		[[0, 1, 2, 3], [4, 7, 6, 5], [0, 4, 5, 1],
		 [2, 6, 7, 3], [0, 3, 7, 4], [1, 5, 6, 2]]);
}

function icosahedronControlMesh() {
	const t = (1 + Math.sqrt(5)) / 2;
	const s = 1 / Math.sqrt(1 + t * t);
	const v = [[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0],
		[0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
		[t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]]
		.map(p => p.map(x => x * s));
	const f = [[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
		[1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
		[3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
		[4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]];
	return makeControlMesh(v, f);
}

function tetrahedronControlMesh() {
	return makeControlMesh(
		[[1, 1, 1], [-1, -1, 1], [-1, 1, -1], [1, -1, -1]],
		[[0, 1, 2], [0, 3, 1], [0, 2, 3], [1, 3, 2]]);
}

// A larger "stress" base mesh: an NxN grid of quads folded onto a box-ish
// surface, used to make the memory difference between streaming and bulk
// visible.
function gridControlMesh(n) {
	const verts = [];
	for (let y = 0; y <= n; y++) {
		for (let x = 0; x <= n; x++) {
			const u = (x / n) * 2 - 1;
			const v = (y / n) * 2 - 1;
			verts.push([u, v, 0.3 * Math.sin(u * 3) * Math.cos(v * 3)]);
		}
	}
	const w = n + 1;
	const faces = [];
	for (let y = 0; y < n; y++) {
		for (let x = 0; x < n; x++) {
			faces.push([y * w + x, y * w + x + 1, (y + 1) * w + x + 1, (y + 1) * w + x]);
		}
	}
	return makeControlMesh(verts, faces);
}

// ===========================================================================
// WASM streaming subdivision
// ===========================================================================

let Module = null;
let streamer = null;

const SCHEME_ID = { catmullclark: 0, loop: 1, bilinear: 2 };
const BOUNDARY_ID = { edgeAndCorner: 0, edgeOnly: 1, none: 2 };

function concatTyped(chunks, total, Ctor) {
	const out = new Ctor(total);
	let off = 0;
	for (const c of chunks) { out.set(c, off); off += c.length; }
	return out;
}

// Runs the wasm streaming refinement and returns interleaved buffers built by
// concatenating the zero-copy batches.
function refine(controlMesh, opts) {
	const posChunks = [], nrmChunks = [], idxChunks = [];
	let vertOffset = 0, totalVerts = 0, totalIdx = 0, batches = 0;

	const onBatch = (pos, nrm, idx, fsrc, nverts, nfaces, bidx) => {
		posChunks.push(pos.slice());            // slice() copies out of the heap
		if (nrm) nrmChunks.push(nrm.slice());
		const off = new Uint32Array(idx.length);
		for (let i = 0; i < idx.length; i++) off[i] = idx[i] + vertOffset;
		idxChunks.push(off);
		vertOffset += nverts;
		totalVerts += nverts;
		totalIdx += idx.length;
		batches++;
	};

	const t0 = performance.now();
	const err = streamer.refineStream(
		controlMesh.points, controlMesh.fvc, controlMesh.fvi,
		opts.scheme, opts.boundary, opts.level, opts.batchFaces,
		opts.wantNormals, onBatch);
	const t1 = performance.now();
	if (err) { throw new Error(err); }

	return {
		positions: concatTyped(posChunks, totalVerts * 3, Float32Array),
		normals: opts.wantNormals ? concatTyped(nrmChunks, totalVerts * 3, Float32Array) : null,
		indices: concatTyped(idxChunks, totalIdx, Uint32Array),
		numVerts: totalVerts,
		numTris: totalIdx / 3,
		batches,
		timeMs: t1 - t0,
		heapBytes: streamer.heapBytes(),
	};
}

// ===========================================================================
// THREE.js geometry
// ===========================================================================

function buildMesh(refined, wireframe) {
	const geometry = new THREE.BufferGeometry();
	geometry.setAttribute('position', new THREE.BufferAttribute(refined.positions, 3));
	geometry.setIndex(new THREE.BufferAttribute(refined.indices, 1));
	if (refined.normals) {
		geometry.setAttribute('normal', new THREE.BufferAttribute(refined.normals, 3));
	} else {
		geometry.computeVertexNormals();
	}
	const material = wireframe
		? new THREE.MeshBasicMaterial({ color: 0x00ff00, wireframe: true })
		: new THREE.MeshStandardMaterial({
			color: 0x6bb6ff, roughness: 0.5, metalness: 0.2, flatShading: false });
	const mesh = new THREE.Mesh(geometry, material);
	mesh.castShadow = true;
	mesh.receiveShadow = true;
	return mesh;
}

// ===========================================================================
// State + update
// ===========================================================================

let currentMesh = null;
let wireframeMesh = null;

const state = {
	subdivisionLevel: 2,
	scheme: 'catmullclark',
	boundary: 'edgeAndCorner',
	baseMesh: 'cube',
	wantNormals: true,
	streaming: true,       // small batches (bounded heap) vs one big batch
	batchFaces: 2048,
	showWireframe: true,
};

function getControlMesh() {
	if (state.scheme === 'loop') {
		return state.baseMesh === 'tetrahedron'
			? tetrahedronControlMesh() : icosahedronControlMesh();
	}
	if (state.baseMesh === 'grid') return gridControlMesh(16);
	return cubeControlMesh();
}

function disposeMesh(m) {
	if (!m) return;
	scene.remove(m);
	m.geometry.dispose();
	m.material.dispose();
}

function updateMesh() {
	if (!streamer) return;
	disposeMesh(currentMesh);
	disposeMesh(wireframeMesh);
	currentMesh = null;
	wireframeMesh = null;

	const cm = getControlMesh();
	const opts = {
		scheme: SCHEME_ID[state.scheme],
		boundary: BOUNDARY_ID[state.boundary],
		level: state.subdivisionLevel,
		// Streaming bounds the wasm working set; "off" pushes everything into
		// one giant batch (bulk-like peak) to make the difference visible.
		batchFaces: state.streaming ? state.batchFaces : 1 << 30,
		wantNormals: state.wantNormals,
	};

	let refined;
	try {
		refined = refine(cm, opts);
	} catch (e) {
		console.error('subdivision failed:', e.message);
		setStatus('Error: ' + e.message);
		return;
	}

	currentMesh = buildMesh(refined, false);
	scene.add(currentMesh);
	if (state.showWireframe) {
		wireframeMesh = buildMesh(refined, true);
		scene.add(wireframeMesh);
	}

	updateStats(refined);
}

function setText(id, text) {
	const el = document.getElementById(id);
	if (el) el.textContent = text;
}

function setStatus(text) { setText('status', text); }

function updateStats(refined) {
	setText('vertCount', refined.numVerts.toLocaleString());
	setText('faceCount', (refined.numTris / 2 | 0).toLocaleString());
	setText('triCount', refined.numTris.toLocaleString());
	setText('levelDisplay', String(state.subdivisionLevel));
	setText('batchCount', refined.batches.toLocaleString());
	setText('heapMB', (refined.heapBytes / (1024 * 1024)).toFixed(1) + ' MB');
	setText('timeMs', refined.timeMs.toFixed(1) + ' ms');
	setStatus(state.streaming
		? `streaming (${state.batchFaces} faces/batch)`
		: 'bulk (single batch)');
}

// ===========================================================================
// GUI
// ===========================================================================

const gui = new GUI();
gui.add(state, 'subdivisionLevel', 0, 7, 1).name('Subdivision Level').onChange(updateMesh);
gui.add(state, 'scheme', ['catmullclark', 'loop', 'bilinear']).name('Scheme').onChange(updateMesh);
gui.add(state, 'boundary', ['edgeAndCorner', 'edgeOnly', 'none']).name('Boundary').onChange(updateMesh);
gui.add(state, 'baseMesh', ['cube', 'grid', 'tetrahedron', 'icosahedron']).name('Base Mesh').onChange(updateMesh);
gui.add(state, 'wantNormals').name('Analytic Normals').onChange(updateMesh);
gui.add(state, 'showWireframe').name('Show Wireframe').onChange(updateMesh);

const memFolder = gui.addFolder('Memory (wasm streaming)');
memFolder.add(state, 'streaming').name('Streaming').onChange(updateMesh);
memFolder.add(state, 'batchFaces', [256, 1024, 2048, 8192, 65536]).name('Faces / batch').onChange(updateMesh);
memFolder.open();

// ===========================================================================
// Animation / resize / init
// ===========================================================================

function animate() {
	requestAnimationFrame(animate);
	controls.update();
	renderer.render(scene, camera);
}

window.addEventListener('resize', () => {
	camera.aspect = window.innerWidth / window.innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(window.innerWidth, window.innerHeight);
});

async function main() {
	setStatus('loading wasm…');
	Module = await initTinyUSDZ();
	streamer = new Module.SubdivStreamer();
	setStatus('ready');
	updateMesh();
	animate();
	console.log('TinyUSDZ streaming subdivision demo loaded');
}

main().catch((e) => {
	console.error(e);
	setStatus('failed to load wasm: ' + e.message);
});
