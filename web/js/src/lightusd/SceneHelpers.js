/**
 * SceneHelpers.js — Blender-style gizmo helpers for lights and cameras
 *
 * Provides visual representations (wireframe helpers) for USD light and camera
 * prims that otherwise appear as empty Group nodes in Three.js.
 */

import * as THREE from 'three';

/**
 * Create a Blender-style point light helper: small sphere + 3 dashed circles.
 * @param {Object} opts
 * @param {number} [opts.color=0xFFCC44] - Helper color
 * @param {number} [opts.scale=1.0] - Overall scale factor
 * @returns {THREE.Group}
 */
export function createPointLightHelper({ color = 0xFFCC44, scale = 1.0 } = {}) {
	const group = new THREE.Group();
	group.name = '_lightHelper';

	// Center dot
	const sphereGeo = new THREE.SphereGeometry(0.03 * scale, 8, 8);
	const sphereMat = new THREE.MeshBasicMaterial({ color });
	group.add(new THREE.Mesh(sphereGeo, sphereMat));

	// 3 dashed circles on XY, XZ, YZ planes
	const segments = 48;
	const radius = 0.3 * scale;
	const dashMat = new THREE.LineDashedMaterial({
		color,
		dashSize: 0.05 * scale,
		gapSize: 0.03 * scale,
	});

	const planes = [
		// [axis-a, axis-b] pairs for circle vertex generation
		{ a: 'x', b: 'y' }, // XY plane
		{ a: 'x', b: 'z' }, // XZ plane
		{ a: 'y', b: 'z' }, // YZ plane
	];

	for (const plane of planes) {
		const points = [];
		for (let i = 0; i <= segments; i++) {
			const theta = (i / segments) * Math.PI * 2;
			const v = new THREE.Vector3();
			v[plane.a] = Math.cos(theta) * radius;
			v[plane.b] = Math.sin(theta) * radius;
			points.push(v);
		}
		const geo = new THREE.BufferGeometry().setFromPoints(points);
		const line = new THREE.Line(geo, dashMat);
		line.computeLineDistances();
		group.add(line);
	}

	return group;
}

/**
 * Create a Blender-style camera helper: pyramid frustum + up-triangle.
 * Camera looks down -Z in USD local space.
 * @param {Object} opts
 * @param {number} [opts.yfov] - Vertical FOV in radians (default: 50mm on 15.29mm sensor)
 * @param {number} [opts.aspectRatio] - Width/height (default: 20.965/15.2908)
 * @param {number} [opts.scale=1.0] - Overall scale factor
 * @param {number} [opts.color=0x88AAFF] - Helper color
 * @returns {THREE.Group}
 */
export function createCameraHelper({
	yfov = 2.0 * Math.atan(0.5 * 15.2908 / 50.0),
	aspectRatio = 20.965 / 15.2908,
	scale = 1.0,
	color = 0x88AAFF,
} = {}) {
	const group = new THREE.Group();
	group.name = '_cameraHelper';

	const depth = 0.5 * scale;
	const halfH = Math.tan(yfov / 2) * depth;
	const halfW = halfH * aspectRatio;

	// Near-plane corners at z = -depth
	const tl = new THREE.Vector3(-halfW, halfH, -depth);
	const tr = new THREE.Vector3(halfW, halfH, -depth);
	const bl = new THREE.Vector3(-halfW, -halfH, -depth);
	const br = new THREE.Vector3(halfW, -halfH, -depth);
	const origin = new THREE.Vector3(0, 0, 0);

	// Up-triangle above near-plane top edge
	const triH = halfH * 0.4;
	const triBase = halfW * 0.5;
	const triY = halfH + triH * 0.1;
	const triTop = new THREE.Vector3(0, triY + triH, -depth);
	const triLeft = new THREE.Vector3(-triBase, triY, -depth);
	const triRight = new THREE.Vector3(triBase, triY, -depth);

	// Line segments as pairs of vertices
	const vertices = [
		// 4 frustum edges: origin -> corners
		origin, tl,
		origin, tr,
		origin, bl,
		origin, br,
		// 4 near-plane edges
		tl, tr,
		tr, br,
		br, bl,
		bl, tl,
		// 3 up-triangle edges
		triLeft, triRight,
		triRight, triTop,
		triTop, triLeft,
	];

	const positions = new Float32Array(vertices.length * 3);
	for (let i = 0; i < vertices.length; i++) {
		positions[i * 3] = vertices[i].x;
		positions[i * 3 + 1] = vertices[i].y;
		positions[i * 3 + 2] = vertices[i].z;
	}

	const geo = new THREE.BufferGeometry();
	geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));

	const mat = new THREE.LineBasicMaterial({ color });
	group.add(new THREE.LineSegments(geo, mat));

	return group;
}

/**
 * Traverse the Three.js scene tree and attach visual helpers to light/camera nodes.
 * @param {THREE.Object3D} threeRoot - Root of the built Three.js scene
 * @param {Object} usdLoader - The WASM USD loader instance (must have getLight/getCamera)
 * @param {Object} opts
 * @param {number} [opts.scale=1.0] - Helper scale
 * @param {boolean} [opts.showLights=true] - Attach light helpers
 * @param {boolean} [opts.showCameras=true] - Attach camera helpers
 * @returns {THREE.Group[]} Array of helper groups for toggling visibility
 */
export function attachSceneHelpers(threeRoot, usdLoader, {
	scale = 1.0,
	showLights = true,
	showCameras = true,
} = {}) {
	const helpers = [];

	threeRoot.traverse((node) => {
		const category = node.userData['nodeCategory'];
		const contentId = node.userData['contentId'];

		if (showLights && category === 'light' && contentId >= 0) {
			if (typeof usdLoader.getLight === 'function') {
				const lightData = usdLoader.getLight(contentId);
				if (lightData && !lightData.error) {
					// Skip dome/environment lights — they have no meaningful position
					const t = lightData.type;
					if (t === 'dome') return;

					let lightColor = 0xFFCC44;
					if (lightData.color) {
						const [r, g, b] = lightData.color;
						lightColor = new THREE.Color(r, g, b).getHex();
					}
					const helper = createPointLightHelper({ color: lightColor, scale });
					node.add(helper);
					helpers.push(helper);
				}
			}
		}

		if (showCameras && category === 'camera' && contentId >= 0) {
			let camOpts = { scale };
			if (typeof usdLoader.getCamera === 'function') {
				const camData = usdLoader.getCamera(contentId);
				if (camData && !camData.error) {
					if (camData.yfov) camOpts.yfov = camData.yfov;
					if (camData.aspectRatio) camOpts.aspectRatio = camData.aspectRatio;
				}
			}
			const helper = createCameraHelper(camOpts);
			node.add(helper);
			helpers.push(helper);
		}
	});

	return helpers;
}
