// three.js-side post-process optimizations applied to a built scene graph.
//
// These complement the native (Tydra) render-scene optimizations:
//   - Native material dedup reduces the RenderScene material *records*.
//   - dedupMaterialsByContent() collapses the resulting THREE.Material
//     *instances* by visual content, so three.js compiles fewer shader
//     programs and BatchedMesh can find same-material groups.
//   - Native mesh-merge concatenates geometry (bakes transforms, loses
//     per-instance identity).
//   - batchByMaterial() uses THREE.BatchedMesh instead: one draw call per
//     material+attribute group via multi-draw, while keeping per-instance
//     transforms and identity (for raycasting / selection).
//
// Ported from service_MeshBatching_DedupMaterials.ts /
// service_MeshBatching_BatchByMaterial.ts.

import * as THREE from 'three';

const round = (v, decimals = 4) => {
	const f = 10 ** decimals;
	return Math.round(v * f) / f;
};

// Content signature for a material. Only MeshStandard/Physical are deduped by
// content; anything else falls back to its uuid so it never collides.
function materialContentKey(m) {
	const isStandard = m.isMeshStandardMaterial;
	const isPhysical = m.isMeshPhysicalMaterial;
	if (!isStandard && !isPhysical) return `__type_uuid__:${m.uuid}`;

	const parts = [
		m.type,
		m.color ? m.color.getHex() : null,
		m.map ? m.map.uuid : null,
		round(m.opacity),
		m.transparent ? 1 : 0,
		m.side,
		m.normalMap ? m.normalMap.uuid : null,
		round(m.normalScale ? m.normalScale.x : 1),
		round(m.normalScale ? m.normalScale.y : 1),
		round(m.roughness != null ? m.roughness : 1),
		m.roughnessMap ? m.roughnessMap.uuid : null,
		round(m.metalness != null ? m.metalness : 0),
		m.metalnessMap ? m.metalnessMap.uuid : null,
		m.aoMap ? m.aoMap.uuid : null,
		round(m.aoMapIntensity != null ? m.aoMapIntensity : 1),
		m.emissive ? m.emissive.getHex() : null,
		m.emissiveMap ? m.emissiveMap.uuid : null,
		round(m.emissiveIntensity != null ? m.emissiveIntensity : 1),
		m.alphaMap ? m.alphaMap.uuid : null,
		m.alphaTest != null ? m.alphaTest : 0,
		m.depthTest ? 1 : 0,
		m.depthWrite ? 1 : 0
	];
	if (isPhysical) {
		parts.push(
			round(m.ior != null ? m.ior : 1.5),
			round(m.clearcoat != null ? m.clearcoat : 0),
			round(m.clearcoatRoughness != null ? m.clearcoatRoughness : 0),
			round(m.sheen != null ? m.sheen : 0),
			round(m.transmission != null ? m.transmission : 0),
			round(m.thickness != null ? m.thickness : 0),
			round(m.iridescence != null ? m.iridescence : 0)
		);
	}
	return parts.join('|');
}

// Collapse visually-identical THREE.Material instances under `root` to a single
// shared instance. Returns { uniqueMaterials, replacedBindings, totalBindings }.
export function dedupMaterialsByContent(root) {
	const canonical = new Map();
	let replaced = 0;
	let total = 0;

	root.traverse((obj) => {
		const mesh = obj;
		if (!mesh.isMesh || !mesh.material) return;

		if (Array.isArray(mesh.material)) {
			for (let i = 0; i < mesh.material.length; i++) {
				total++;
				const mat = mesh.material[i];
				const key = materialContentKey(mat);
				const cached = canonical.get(key);
				if (cached && cached !== mat) {
					mesh.material[i] = cached;
					replaced++;
				} else if (!cached) {
					canonical.set(key, mat);
				}
			}
			return;
		}

		total++;
		const key = materialContentKey(mesh.material);
		const cached = canonical.get(key);
		if (cached && cached !== mesh.material) {
			mesh.material = cached;
			replaced++;
		} else if (!cached) {
			canonical.set(key, mesh.material);
		}
	});

	return { uniqueMaterials: canonical.size, replacedBindings: replaced, totalBindings: total };
}

function attributeSignature(geom) {
	return Object.keys(geom.attributes).sort().join(',');
}

function isBatchable(mesh, skip) {
	if (mesh.isSkinnedMesh) { skip.skinned++; return false; }
	if (Array.isArray(mesh.material)) { skip.materialArray++; return false; }
	if (!mesh.material) { skip.noMaterial++; return false; }
	if (!mesh.geometry || !mesh.geometry.index) { skip.noIndex++; return false; }
	return true;
}

// Replace eligible meshes under `root` with THREE.BatchedMesh groupings keyed on
// material identity + attribute signature. Per-instance transforms are baked
// into BatchedMesh instance matrices (world space) and identity is preserved in
// userData.batchedInstances. Returns batching statistics.
export function batchByMaterial(root) {
	root.updateMatrixWorld(true);

	const skipReasons = {
		skinned: 0, materialArray: 0, noIndex: 0, noMaterial: 0, singletonGroup: 0
	};

	const eligible = [];
	root.traverse((obj) => {
		if (obj.isMesh && !obj.isBatchedMesh && isBatchable(obj, skipReasons)) {
			eligible.push(obj);
		}
	});

	// Group: same material instance AND same attribute set (BatchedMesh requires
	// a consistent attribute layout across a batch). Run dedupMaterialsByContent
	// first so material.uuid actually groups visually-identical materials.
	const groups = new Map();
	for (const mesh of eligible) {
		const key = `${mesh.material.uuid}|${attributeSignature(mesh.geometry)}`;
		const list = groups.get(key) || [];
		list.push(mesh);
		groups.set(key, list);
	}

	const batches = [];
	let batchedMeshCount = 0;
	let batchCount = 0;

	for (const meshes of groups.values()) {
		if (meshes.length < 2) {
			skipReasons.singletonGroup += meshes.length;
			continue;
		}

		let totalVerts = 0;
		let totalIndices = 0;
		for (const m of meshes) {
			totalVerts += m.geometry.attributes.position.count;
			totalIndices += m.geometry.index.count;
		}

		const material = meshes[0].material;
		const batched = new THREE.BatchedMesh(
			meshes.length, totalVerts, totalIndices, material);

		const instances = new Map();
		for (const mesh of meshes) {
			try {
				const geomId = batched.addGeometry(mesh.geometry);
				const instanceId = batched.addInstance(geomId);
				batched.setMatrixAt(instanceId, mesh.matrixWorld);
				instances.set(instanceId, { name: mesh.name, userData: { ...mesh.userData } });
				mesh.removeFromParent();
				batchedMeshCount++;
			} catch (err) {
				console.warn(`[MeshBatching] Failed to add mesh "${mesh.name}" to batch:`, err);
			}
		}

		batched.userData.batchedInstances = instances;
		batched.userData.isBatched = true;
		batches.push(batched);
		batchCount++;
	}

	// BatchedMesh world transform is identity; per-instance matrices already hold
	// the original world-space transforms.
	for (const b of batches) {
		root.add(b);
	}

	return {
		batchedMeshCount,
		batchCount,
		skippedMeshCount: eligible.length - batchedMeshCount,
		skipReasons
	};
}
