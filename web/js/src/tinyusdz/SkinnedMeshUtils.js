/**
 * Skinned Mesh Utilities for Three.js + USD
 *
 * Runtime utilities for Three.js SkinnedMesh objects:
 * bounding box computation, raycasting against deformed poses,
 * scene graph helpers, and in-place mesh replacement.
 *
 * @module SkinnedMeshUtils
 */

import * as THREE from 'three';

// Reusable temporaries to reduce per-frame GC pressure
const _tmpVec3 = new THREE.Vector3();
const _tmpVec3b = new THREE.Vector3();
const _tmpVec3c = new THREE.Vector3();
const _tmpBox3b = new THREE.Box3();

// WeakMap cache for per-mesh bone indices (avoids recomputing each frame)
const _meshBoneIndexCache = new WeakMap();

/**
 * Find a node in the Three.js hierarchy by matching a USD prim path.
 * E.g., path="/root/Armature/Armature_001", root.name="root" -> finds Armature_001 node.
 * @param {THREE.Object3D} root - Root of the Three.js hierarchy
 * @param {string} usdPath - USD absolute prim path (e.g., "/root/Armature/Skeleton")
 * @returns {THREE.Object3D|null}
 */
export function findNodeByUSDPath(root, usdPath) {
	const parts = usdPath.replace(/^\//, '').split('/').filter(p => p.length > 0);
	if (parts.length === 0) return null;

	let current = root;
	// If root's name matches the first path component, start matching from the second
	const startIdx = (current.name === parts[0]) ? 1 : 0;

	for (let i = startIdx; i < parts.length; i++) {
		const child = current.children.find(c => c.name === parts[i]);
		if (!child) {
			console.warn(`findNodeByUSDPath: could not find "${parts[i]}" under "${current.name}" (path: ${usdPath})`);
			return null;
		}
		current = child;
	}
	return current;
}

/**
 * Replace a THREE.Mesh with a THREE.SkinnedMesh in-place within its parent hierarchy.
 * Copies geometry, material, name, transform, and children.
 * @param {THREE.Mesh} mesh - The original mesh to replace
 * @returns {THREE.SkinnedMesh} The new SkinnedMesh
 */
export function replaceWithSkinnedMesh(mesh) {
	const parent = mesh.parent;
	const skinnedMesh = new THREE.SkinnedMesh(mesh.geometry, mesh.material);
	skinnedMesh.name = mesh.name;
	skinnedMesh.position.copy(mesh.position);
	skinnedMesh.quaternion.copy(mesh.quaternion);
	skinnedMesh.scale.copy(mesh.scale);
	skinnedMesh.frustumCulled = false; // Skinned mesh bbox can be stale

	// Move children from original to new
	while (mesh.children.length > 0) {
		skinnedMesh.add(mesh.children[0]);
	}

	// Replace in parent
	if (parent) {
		parent.remove(mesh);
		parent.add(skinnedMesh);
	}

	return skinnedMesh;
}

/**
 * Compute scene bounding box from meshes (static state, ignores skinning deformation)
 * @param {THREE.Object3D} root - Root object to traverse
 * @returns {THREE.Box3} Bounding box
 */
export function computeSceneBoundingBox(root) {
	const box = new THREE.Box3();

	root.traverse((child) => {
		if (child.isMesh && child.geometry) {
			child.geometry.computeBoundingBox();
			if (child.geometry.boundingBox) {
				_tmpBox3b.copy(child.geometry.boundingBox);
				_tmpBox3b.applyMatrix4(child.matrixWorld);
				box.union(_tmpBox3b);
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
 * Compute skinned bounding box for a mesh using skeleton bone positions
 * @param {THREE.Mesh} mesh - The mesh to compute bbox for
 * @param {THREE.Box3} [targetBox] - Optional box to fill
 * @returns {THREE.Box3} The bounding box
 */
export function computeSkinnedBBox(mesh, targetBox = null) {
	const box = targetBox || new THREE.Box3();
	box.makeEmpty();

	if (mesh.isSkinnedMesh && mesh.skeleton && mesh.skeleton.bones.length > 0) {
		// Use bone world positions for skinned meshes
		expandBoxBySkeletonBones(mesh.skeleton, box);
		// Add margin for mesh volume around bones
		box.getSize(_tmpVec3b);
		_tmpVec3c.copy(_tmpVec3b).multiplyScalar(0.15);
		box.min.sub(_tmpVec3c);
		box.max.add(_tmpVec3c);
	} else {
		// Regular mesh - use geometry bounds
		box.expandByObject(mesh);
	}

	return box;
}

/**
 * Expand bounding box using only the bones that influence a specific mesh
 * @param {THREE.SkinnedMesh} mesh - The skinned mesh
 * @param {THREE.Box3} box - Box to expand
 */
export function expandBoxByMeshBones(mesh, box) {
	if (!mesh.skeleton || !mesh.geometry) return;

	const skinIndex = mesh.geometry.attributes.skinIndex;
	if (!skinIndex) return;

	// Use cached bone indices or compute and cache them
	let usedBoneIndices = _meshBoneIndexCache.get(mesh.geometry);
	if (!usedBoneIndices) {
		const indexSet = new Set();
		for (let i = 0; i < skinIndex.count; i++) {
			for (let j = 0; j < skinIndex.itemSize; j++) {
				indexSet.add(skinIndex.getComponent(i, j));
			}
		}
		usedBoneIndices = Array.from(indexSet); // array for faster iteration
		_meshBoneIndexCache.set(mesh.geometry, usedBoneIndices);
	}

	// Use only the bones that influence this mesh
	for (let i = 0; i < usedBoneIndices.length; i++) {
		const bone = mesh.skeleton.bones[usedBoneIndices[i]];
		if (bone) {
			bone.getWorldPosition(_tmpVec3);
			box.expandByPoint(_tmpVec3);
		}
	}
}

/**
 * Compute bounding box from skeleton bones
 * This is more accurate for skinned meshes than using geometry bounds
 * @param {THREE.Skeleton} skeleton - The skeleton to compute bounds from
 * @param {THREE.Box3} box - Box to expand with bone positions
 */
export function expandBoxBySkeletonBones(skeleton, box) {
	if (!skeleton || !skeleton.bones) return;

	for (const bone of skeleton.bones) {
		bone.getWorldPosition(_tmpVec3);
		box.expandByPoint(_tmpVec3);
	}
}

/**
 * Raycast against skinned meshes using their deformed (post-skinning) positions.
 * Standard raycasting tests against bind-pose geometry, which fails from side views
 * when the animated pose differs significantly from the bind pose.
 *
 * @param {THREE.Raycaster} raycaster - Configured raycaster
 * @param {Array<THREE.Mesh>} meshes - Meshes to test
 * @returns {Array<{distance: number, object: THREE.Mesh, point: THREE.Vector3}>} Sorted intersections
 */
export function raycastSkinnedMeshes(raycaster, meshes) {
	const results = [];
	const _tempPos = new THREE.Vector3();
	const _bbox = new THREE.Box3();

	for (const mesh of meshes) {
		if (!mesh.visible) continue;

		if (mesh.isSkinnedMesh && mesh.skeleton) {
			// Early rejection: check ray against bone-based bounding box
			_bbox.makeEmpty();
			expandBoxByMeshBones(mesh, _bbox);
			if (_bbox.isEmpty()) continue;
			// Pad the bbox slightly for tolerance
			_bbox.expandByScalar(5);
			if (!raycaster.ray.intersectsBox(_bbox)) continue;

			// Compute skinned positions into a temporary buffer, then raycast
			const geo = mesh.geometry;
			const posAttr = geo.attributes.position;
			const indexAttr = geo.index;
			if (!posAttr) continue;

			// Create or reuse a temporary skinned position buffer
			if (!mesh._skinnedPositions || mesh._skinnedPositions.length !== posAttr.count * 3) {
				mesh._skinnedPositions = new Float32Array(posAttr.count * 3);
			}
			const skinned = mesh._skinnedPositions;

			// Compute skinned world positions using applyBoneTransform
			for (let i = 0; i < posAttr.count; i++) {
				_tempPos.fromBufferAttribute(posAttr, i);
				mesh.applyBoneTransform(i, _tempPos);
				_tempPos.applyMatrix4(mesh.matrixWorld);
				skinned[i * 3] = _tempPos.x;
				skinned[i * 3 + 1] = _tempPos.y;
				skinned[i * 3 + 2] = _tempPos.z;
			}

			// Raycast triangles against skinned positions
			const _a = new THREE.Vector3();
			const _b = new THREE.Vector3();
			const _c = new THREE.Vector3();
			const _intersectPoint = new THREE.Vector3();
			const triCount = indexAttr ? indexAttr.count / 3 : posAttr.count / 3;

			for (let t = 0; t < triCount; t++) {
				let ia, ib, ic;
				if (indexAttr) {
					ia = indexAttr.getX(t * 3);
					ib = indexAttr.getX(t * 3 + 1);
					ic = indexAttr.getX(t * 3 + 2);
				} else {
					ia = t * 3;
					ib = t * 3 + 1;
					ic = t * 3 + 2;
				}
				_a.set(skinned[ia * 3], skinned[ia * 3 + 1], skinned[ia * 3 + 2]);
				_b.set(skinned[ib * 3], skinned[ib * 3 + 1], skinned[ib * 3 + 2]);
				_c.set(skinned[ic * 3], skinned[ic * 3 + 1], skinned[ic * 3 + 2]);

				const hit = raycaster.ray.intersectTriangle(_c, _b, _a, false, _intersectPoint);
				if (hit) {
					const dist = raycaster.ray.origin.distanceTo(_intersectPoint);
					if (dist >= raycaster.near && dist <= raycaster.far) {
						results.push({ distance: dist, object: mesh, point: _intersectPoint.clone() });
						break; // One hit per mesh is enough for selection
					}
				}
			}
		} else {
			// Non-skinned mesh: use standard raycasting
			const hits = raycaster.intersectObject(mesh);
			if (hits.length > 0) {
				results.push({ distance: hits[0].distance, object: mesh, point: hits[0].point });
			}
		}
	}

	results.sort((a, b) => a.distance - b.distance);
	return results;
}

export default {
	findNodeByUSDPath,
	replaceWithSkinnedMesh,
	computeSceneBoundingBox,
	computeSkinnedBBox,
	expandBoxByMeshBones,
	expandBoxBySkeletonBones,
	raycastSkinnedMeshes
};
