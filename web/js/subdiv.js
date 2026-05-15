import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';

// ===========================================
// Scene Setup
// ===========================================

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1a1a2e);

// Camera
const camera = new THREE.PerspectiveCamera(
	60,
	window.innerWidth / window.innerHeight,
	0.1,
	1000
);
camera.position.set(3, 3, 5);
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
controls.minDistance = 2;
controls.maxDistance = 20;

// Lighting
const ambientLight = new THREE.AmbientLight(0x404040, 0.5);
scene.add(ambientLight);

const directionalLight1 = new THREE.DirectionalLight(0xffffff, 0.8);
directionalLight1.position.set(5, 10, 7);
directionalLight1.castShadow = true;
scene.add(directionalLight1);

const directionalLight2 = new THREE.DirectionalLight(0x6bb6ff, 0.3);
directionalLight2.position.set(-5, 5, -5);
scene.add(directionalLight2);

// Grid helper
const gridHelper = new THREE.GridHelper(10, 10, 0x444444, 0x222222);
scene.add(gridHelper);

// Axis helper
const axisHelper = new THREE.AxesHelper(2);
scene.add(axisHelper);

// ===========================================
// Subdivision Algorithms
// ===========================================

/**
 * Half-edge data structure for subdivision
 */
class HalfEdgeMesh {
	constructor(vertices, faces) {
		this.vertices = vertices; // Array of [x, y, z] arrays
		this.faces = faces;       // Array of vertex index arrays
		this.halfEdges = [];
		this.buildHalfEdges();
	}

	buildHalfEdges() {
		// Build half-edge structure for efficient subdivision
		const edgeMap = new Map();

		this.faces.forEach((face, faceIdx) => {
			const faceEdges = [];
			for (let i = 0; i < face.length; i++) {
				const v0 = face[i];
				const v1 = face[(i + 1) % face.length];
				const edgeKey = `${Math.min(v0, v1)}_${Math.max(v0, v1)}`;

				if (!edgeMap.has(edgeKey)) {
					edgeMap.set(edgeKey, []);
				}
				edgeMap.get(edgeKey).push({ face: faceIdx, v0, v1 });
			}
		});

		this.edgeMap = edgeMap;
	}

	getEdgeKey(v0, v1) {
		return `${Math.min(v0, v1)}_${Math.max(v0, v1)}`;
	}
}

/**
 * Catmull-Clark Subdivision
 * For quad meshes - produces C² continuous surfaces
 */
function subdivideCatmullClark(mesh) {
	const { vertices, faces } = mesh;
	const newVertices = [...vertices];
	const newFaces = [];

	// Step 1: Compute face points (average of face vertices)
	const facePoints = faces.map(face => {
		const avg = [0, 0, 0];
		face.forEach(vi => {
			avg[0] += vertices[vi][0];
			avg[1] += vertices[vi][1];
			avg[2] += vertices[vi][2];
		});
		const n = face.length;
		return [avg[0] / n, avg[1] / n, avg[2] / n];
	});

	const facePointIndices = facePoints.map((fp, i) => {
		newVertices.push(fp);
		return newVertices.length - 1;
	});

	// Step 2: Compute edge points
	const edgePoints = new Map();
	mesh.edgeMap.forEach((edgeInfo, edgeKey) => {
		const [v0, v1] = edgeKey.split('_').map(Number);

		if (edgeInfo.length === 2) {
			// Interior edge: average of edge vertices + adjacent face points
			const fp0 = facePoints[edgeInfo[0].face];
			const fp1 = facePoints[edgeInfo[1].face];
			const vp0 = vertices[v0];
			const vp1 = vertices[v1];

			const edgePoint = [
				(vp0[0] + vp1[0] + fp0[0] + fp1[0]) / 4,
				(vp0[1] + vp1[1] + fp0[1] + fp1[1]) / 4,
				(vp0[2] + vp1[2] + fp0[2] + fp1[2]) / 4
			];

			newVertices.push(edgePoint);
			edgePoints.set(edgeKey, newVertices.length - 1);
		} else {
			// Boundary edge: average of edge vertices only
			const vp0 = vertices[v0];
			const vp1 = vertices[v1];
			const edgePoint = [
				(vp0[0] + vp1[0]) / 2,
				(vp0[1] + vp1[1]) / 2,
				(vp0[2] + vp1[2]) / 2
			];

			newVertices.push(edgePoint);
			edgePoints.set(edgeKey, newVertices.length - 1);
		}
	});

	// Step 3: Update original vertices
	const updatedVertices = new Map();
	faces.forEach((face, faceIdx) => {
		const fp = facePoints[faceIdx];

		face.forEach((vi, i) => {
			if (!updatedVertices.has(vi)) {
				const v = vertices[vi];

				// Find all faces containing this vertex
				const adjacentFaces = [];
				const adjacentEdges = [];

				faces.forEach((f, fi) => {
					if (f.includes(vi)) {
						adjacentFaces.push(fi);

						// Find edges connected to this vertex
						const idx = f.indexOf(vi);
						const prev = f[(idx - 1 + f.length) % f.length];
						const next = f[(idx + 1) % f.length];

						const key1 = mesh.getEdgeKey(vi, prev);
						const key2 = mesh.getEdgeKey(vi, next);

						if (!adjacentEdges.includes(key1)) adjacentEdges.push(key1);
						if (!adjacentEdges.includes(key2)) adjacentEdges.push(key2);
					}
				});

				const n = adjacentFaces.length;

				// Compute average of adjacent face points
				const faceAvg = [0, 0, 0];
				adjacentFaces.forEach(fi => {
					faceAvg[0] += facePoints[fi][0];
					faceAvg[1] += facePoints[fi][1];
					faceAvg[2] += facePoints[fi][2];
				});
				faceAvg[0] /= n;
				faceAvg[1] /= n;
				faceAvg[2] /= n;

				// Compute average of adjacent edge midpoints
				const edgeAvg = [0, 0, 0];
				adjacentEdges.forEach(edgeKey => {
					const [e0, e1] = edgeKey.split('_').map(Number);
					const v0 = vertices[e0];
					const v1 = vertices[e1];
					edgeAvg[0] += (v0[0] + v1[0]) / 2;
					edgeAvg[1] += (v0[1] + v1[1]) / 2;
					edgeAvg[2] += (v0[2] + v1[2]) / 2;
				});
				edgeAvg[0] /= adjacentEdges.length;
				edgeAvg[1] /= adjacentEdges.length;
				edgeAvg[2] /= adjacentEdges.length;

				// Catmull-Clark formula: v' = (F + 2R + (n-3)v) / n
				// where F = face average, R = edge average, v = original vertex
				const newV = [
					(faceAvg[0] + 2 * edgeAvg[0] + (n - 3) * v[0]) / n,
					(faceAvg[1] + 2 * edgeAvg[1] + (n - 3) * v[1]) / n,
					(faceAvg[2] + 2 * edgeAvg[2] + (n - 3) * v[2]) / n
				];

				updatedVertices.set(vi, newV);
			}
		});
	});

	// Update vertices in place
	updatedVertices.forEach((newPos, vi) => {
		newVertices[vi] = newPos;
	});

	// Step 4: Create new quad faces
	faces.forEach((face, faceIdx) => {
		const fpi = facePointIndices[faceIdx];

		for (let i = 0; i < face.length; i++) {
			const vi = face[i];
			const vnext = face[(i + 1) % face.length];

			const e1Key = mesh.getEdgeKey(vi, vnext);
			const vprev = face[(i - 1 + face.length) % face.length];
			const e2Key = mesh.getEdgeKey(vprev, vi);

			const ep1 = edgePoints.get(e1Key);
			const ep2 = edgePoints.get(e2Key);

			// Create quad: [vi, ep1, facePoint, ep2]
			newFaces.push([vi, ep1, fpi, ep2]);
		}
	});

	return new HalfEdgeMesh(newVertices, newFaces);
}

/**
 * Loop Subdivision
 * For triangle meshes - produces C¹ continuous surfaces
 */
function subdivideLoop(mesh) {
	const { vertices, faces } = mesh;
	const newVertices = [...vertices];
	const newFaces = [];

	// Step 1: Compute edge vertices (odd vertices)
	const edgeVertices = new Map();
	mesh.edgeMap.forEach((edgeInfo, edgeKey) => {
		const [v0, v1] = edgeKey.split('_').map(Number);

		if (edgeInfo.length === 2) {
			// Interior edge: use Loop subdivision rule
			// New vertex = 3/8 * (A + B) + 1/8 * (C + D)
			// where A, B are edge vertices and C, D are opposite vertices
			const face0 = faces[edgeInfo[0].face];
			const face1 = faces[edgeInfo[1].face];

			const opposite0 = face0.find(v => v !== v0 && v !== v1);
			const opposite1 = face1.find(v => v !== v0 && v !== v1);

			const vp0 = vertices[v0];
			const vp1 = vertices[v1];
			const vp2 = vertices[opposite0];
			const vp3 = vertices[opposite1];

			const edgeVertex = [
				3/8 * (vp0[0] + vp1[0]) + 1/8 * (vp2[0] + vp3[0]),
				3/8 * (vp0[1] + vp1[1]) + 1/8 * (vp2[1] + vp3[1]),
				3/8 * (vp0[2] + vp1[2]) + 1/8 * (vp2[2] + vp3[2])
			];

			newVertices.push(edgeVertex);
			edgeVertices.set(edgeKey, newVertices.length - 1);
		} else {
			// Boundary edge: simple midpoint
			const vp0 = vertices[v0];
			const vp1 = vertices[v1];
			const edgeVertex = [
				(vp0[0] + vp1[0]) / 2,
				(vp0[1] + vp1[1]) / 2,
				(vp0[2] + vp1[2]) / 2
			];

			newVertices.push(edgeVertex);
			edgeVertices.set(edgeKey, newVertices.length - 1);
		}
	});

	// Step 2: Update original vertices (even vertices)
	const updatedVertices = new Map();
	vertices.forEach((v, vi) => {
		// Find all adjacent vertices
		const neighbors = new Set();
		faces.forEach(face => {
			if (face.includes(vi)) {
				face.forEach(fv => {
					if (fv !== vi) neighbors.add(fv);
				});
			}
		});

		const n = neighbors.size;

		// Loop subdivision weight beta
		let beta;
		if (n === 3) {
			beta = 3/16;
		} else {
			beta = 3 / (8 * n);
		}

		// Compute weighted average
		const newV = [
			v[0] * (1 - n * beta),
			v[1] * (1 - n * beta),
			v[2] * (1 - n * beta)
		];

		neighbors.forEach(ni => {
			const nv = vertices[ni];
			newV[0] += beta * nv[0];
			newV[1] += beta * nv[1];
			newV[2] += beta * nv[2];
		});

		updatedVertices.set(vi, newV);
	});

	// Update vertices in place
	updatedVertices.forEach((newPos, vi) => {
		newVertices[vi] = newPos;
	});

	// Step 3: Create new triangle faces
	faces.forEach(face => {
		if (face.length !== 3) {
			console.warn('Loop subdivision requires triangle faces');
			return;
		}

		const [v0, v1, v2] = face;

		// Get edge vertices
		const e01 = edgeVertices.get(mesh.getEdgeKey(v0, v1));
		const e12 = edgeVertices.get(mesh.getEdgeKey(v1, v2));
		const e20 = edgeVertices.get(mesh.getEdgeKey(v2, v0));

		// Create 4 new triangles
		newFaces.push([v0, e01, e20]);
		newFaces.push([v1, e12, e01]);
		newFaces.push([v2, e20, e12]);
		newFaces.push([e01, e12, e20]); // Center triangle
	});

	return new HalfEdgeMesh(newVertices, newFaces);
}

/**
 * Bilinear Subdivision
 * Simple linear interpolation - no smoothing
 */
function subdivideBilinear(mesh) {
	const { vertices, faces } = mesh;
	const newVertices = [...vertices];
	const newFaces = [];

	// Step 1: Add edge midpoints
	const edgeMidpoints = new Map();
	mesh.edgeMap.forEach((edgeInfo, edgeKey) => {
		const [v0, v1] = edgeKey.split('_').map(Number);
		const vp0 = vertices[v0];
		const vp1 = vertices[v1];

		const midpoint = [
			(vp0[0] + vp1[0]) / 2,
			(vp0[1] + vp1[1]) / 2,
			(vp0[2] + vp1[2]) / 2
		];

		newVertices.push(midpoint);
		edgeMidpoints.set(edgeKey, newVertices.length - 1);
	});

	// Step 2: Add face centers
	const faceCenters = faces.map(face => {
		const center = [0, 0, 0];
		face.forEach(vi => {
			center[0] += vertices[vi][0];
			center[1] += vertices[vi][1];
			center[2] += vertices[vi][2];
		});
		const n = face.length;
		return [center[0] / n, center[1] / n, center[2] / n];
	});

	const faceCenterIndices = faceCenters.map(fc => {
		newVertices.push(fc);
		return newVertices.length - 1;
	});

	// Step 3: Create new faces
	faces.forEach((face, faceIdx) => {
		const fci = faceCenterIndices[faceIdx];

		for (let i = 0; i < face.length; i++) {
			const vi = face[i];
			const vnext = face[(i + 1) % face.length];

			const e1Key = mesh.getEdgeKey(vi, vnext);
			const vprev = face[(i - 1 + face.length) % face.length];
			const e2Key = mesh.getEdgeKey(vprev, vi);

			const emp1 = edgeMidpoints.get(e1Key);
			const emp2 = edgeMidpoints.get(e2Key);

			// Create quad or triangle based on original face
			if (face.length === 4) {
				newFaces.push([vi, emp1, fci, emp2]);
			} else {
				newFaces.push([vi, emp1, fci, emp2]);
			}
		}
	});

	return new HalfEdgeMesh(newVertices, newFaces);
}

/**
 * Apply subdivision N times
 */
function subdivide(vertices, faces, level, scheme) {
	if (level <= 0) {
		return { vertices, faces };
	}

	let mesh = new HalfEdgeMesh(vertices, faces);

	for (let i = 0; i < level; i++) {
		if (scheme === 'catmullclark') {
			mesh = subdivideCatmullClark(mesh);
		} else if (scheme === 'loop') {
			mesh = subdivideLoop(mesh);
		} else if (scheme === 'bilinear') {
			mesh = subdivideBilinear(mesh);
		}
	}

	return { vertices: mesh.vertices, faces: mesh.faces };
}

// ===========================================
// Control Mesh Creation
// ===========================================

/**
 * Create a simple cube control mesh (for Catmull-Clark)
 */
function createCubeControlMesh() {
	const vertices = [
		[-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1], // Back face
		[-1, -1,  1], [1, -1,  1], [1, 1,  1], [-1, 1,  1]  // Front face
	];

	const faces = [
		[0, 1, 2, 3], // Back
		[4, 7, 6, 5], // Front
		[0, 4, 5, 1], // Bottom
		[2, 6, 7, 3], // Top
		[0, 3, 7, 4], // Left
		[1, 5, 6, 2]  // Right
	];

	return { vertices, faces };
}

/**
 * Create an icosahedron control mesh (for Loop)
 */
function createIcosahedronControlMesh() {
	const t = (1 + Math.sqrt(5)) / 2; // Golden ratio
	const scale = 1 / Math.sqrt(1 + t * t); // Normalize

	const vertices = [
		[-1,  t,  0], [ 1,  t,  0], [-1, -t,  0], [ 1, -t,  0],
		[ 0, -1,  t], [ 0,  1,  t], [ 0, -1, -t], [ 0,  1, -t],
		[ t,  0, -1], [ t,  0,  1], [-t,  0, -1], [-t,  0,  1]
	].map(v => v.map(x => x * scale));

	const faces = [
		[0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
		[1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
		[3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
		[4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]
	];

	return { vertices, faces };
}

/**
 * Create a tetrahedron control mesh (simpler for Loop)
 */
function createTetrahedronControlMesh() {
	const vertices = [
		[1, 1, 1],
		[-1, -1, 1],
		[-1, 1, -1],
		[1, -1, -1]
	];

	const faces = [
		[0, 1, 2],
		[0, 3, 1],
		[0, 2, 3],
		[1, 3, 2]
	];

	return { vertices, faces };
}

// ===========================================
// Three.js Mesh Creation
// ===========================================

/**
 * Convert custom mesh format to Three.js geometry
 */
function createThreeJSMesh(vertices, faces, wireframe = false) {
	const geometry = new THREE.BufferGeometry();

	// Triangulate faces
	const positions = [];
	const indices = [];
	let vertexIndex = 0;

	// Add all vertices
	vertices.forEach(v => {
		positions.push(v[0], v[1], v[2]);
	});

	// Triangulate faces (simple fan triangulation)
	faces.forEach(face => {
		if (face.length === 3) {
			indices.push(face[0], face[1], face[2]);
		} else if (face.length === 4) {
			// Quad -> 2 triangles
			indices.push(face[0], face[1], face[2]);
			indices.push(face[0], face[2], face[3]);
		} else {
			// N-gon -> fan triangulation
			for (let i = 1; i < face.length - 1; i++) {
				indices.push(face[0], face[i], face[i + 1]);
			}
		}
	});

	geometry.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
	geometry.setIndex(indices);
	geometry.computeVertexNormals();

	const material = wireframe
		? new THREE.MeshBasicMaterial({ color: 0x00ff00, wireframe: true })
		: new THREE.MeshStandardMaterial({
			color: 0x6bb6ff,
			roughness: 0.5,
			metalness: 0.2,
			flatShading: false
		});

	const mesh = new THREE.Mesh(geometry, material);
	mesh.castShadow = true;
	mesh.receiveShadow = true;

	return mesh;
}

// ===========================================
// GUI and State Management
// ===========================================

let currentMesh = null;
let wireframeMesh = null;

const state = {
	subdivisionLevel: 2,
	scheme: 'catmullclark',
	showWireframe: true,
	baseMesh: 'cube' // 'cube', 'icosahedron', 'tetrahedron'
};

function updateMesh() {
	// Remove old mesh
	if (currentMesh) {
		scene.remove(currentMesh);
		currentMesh.geometry.dispose();
		currentMesh.material.dispose();
	}
	if (wireframeMesh) {
		scene.remove(wireframeMesh);
		wireframeMesh.geometry.dispose();
		wireframeMesh.material.dispose();
	}

	// Get base mesh
	let controlMesh;
	if (state.scheme === 'loop') {
		controlMesh = state.baseMesh === 'tetrahedron'
			? createTetrahedronControlMesh()
			: createIcosahedronControlMesh();
	} else {
		controlMesh = createCubeControlMesh();
	}

	// Apply subdivision
	const subdivided = subdivide(
		controlMesh.vertices,
		controlMesh.faces,
		state.subdivisionLevel,
		state.scheme
	);

	// Create Three.js mesh
	currentMesh = createThreeJSMesh(subdivided.vertices, subdivided.faces, false);
	scene.add(currentMesh);

	// Create wireframe
	if (state.showWireframe) {
		wireframeMesh = createThreeJSMesh(subdivided.vertices, subdivided.faces, true);
		scene.add(wireframeMesh);
	}

	// Update statistics
	updateStats(subdivided.vertices.length, subdivided.faces.length);
}

function updateStats(vertCount, faceCount) {
	document.getElementById('vertCount').textContent = vertCount;
	document.getElementById('faceCount').textContent = faceCount;

	// Calculate triangle count (approximate for quads)
	let triCount = 0;
	if (state.scheme === 'loop') {
		triCount = faceCount;
	} else {
		triCount = faceCount * 2; // Each quad becomes 2 triangles
	}
	document.getElementById('triCount').textContent = triCount;
	document.getElementById('levelDisplay').textContent = state.subdivisionLevel;
}

// ===========================================
// GUI Setup
// ===========================================

const gui = new GUI();

gui.add(state, 'subdivisionLevel', 0, 5, 1)
	.name('Subdivision Level')
	.onChange(() => updateMesh());

gui.add(state, 'scheme', ['catmullclark', 'loop', 'bilinear'])
	.name('Scheme')
	.onChange(() => updateMesh());

gui.add(state, 'baseMesh', ['cube', 'tetrahedron', 'icosahedron'])
	.name('Base Mesh')
	.onChange(() => updateMesh());

gui.add(state, 'showWireframe')
	.name('Show Wireframe')
	.onChange(() => updateMesh());

// ===========================================
// Animation Loop
// ===========================================

function animate() {
	requestAnimationFrame(animate);
	controls.update();
	renderer.render(scene, camera);
}

// ===========================================
// Window Resize Handler
// ===========================================

window.addEventListener('resize', () => {
	camera.aspect = window.innerWidth / window.innerHeight;
	camera.updateProjectionMatrix();
	renderer.setSize(window.innerWidth, window.innerHeight);
});

// ===========================================
// Initialize
// ===========================================

updateMesh();
animate();

console.log('TinyUSDZ Subdivision Surface Demo loaded');
