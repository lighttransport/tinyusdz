import * as THREE from 'three';
import { toOwnedFloat32Array } from './TypedArrayOwnership.js';

/**
 * Extract skinned mesh data from a TinyUSDZ scene object.
 *
 * Copies mesh skinning buffers to JS-owned arrays so data remains valid after
 * usd_scene.delete().
 */
export function extractSkinnedMeshData(usdScene, options = {}) {
  const logger = options.logger || console;
  const verbose = options.verbose !== false;

  const numMeshes = usdScene.numMeshes ? usdScene.numMeshes() : 0;
  if (verbose) {
    logger.log(`=== Mesh Skinning Data (${numMeshes} meshes) ===`);
  }

  let hasSkinnedMeshData = false;
  let firstGeomBindTransform = null;
  const allSkinnedMeshUSDData = new Map();
  // Value `null` means duplicate/ambiguous name.
  const skinnedMeshDataByName = new Map();

  for (let i = 0; i < numMeshes; i++) {
    const mesh = usdScene.getMesh(i);
    const hasJointIndices = mesh.jointIndices && mesh.jointIndices.length > 0;
    const hasJointWeights = mesh.jointWeights && mesh.jointWeights.length > 0;
    if (!hasJointIndices && !hasJointWeights) {
      continue;
    }

    hasSkinnedMeshData = true;

    let geomBindTransformMatrix = null;
    if (mesh.geomBindTransform && mesh.geomBindTransform.length === 16) {
      geomBindTransformMatrix = new THREE.Matrix4();
      geomBindTransformMatrix.fromArray(Array.from(mesh.geomBindTransform));
    }

    const meshAbsPath = mesh.absPath || '';
    const meshName = meshAbsPath ? meshAbsPath.split('/').pop() : `mesh_${i}`;

    const meshData = {
      meshId: i,
      skel_id: mesh.skel_id !== undefined ? mesh.skel_id : 0,
      jointIndices: new Int32Array(mesh.jointIndices),
      jointWeights: toOwnedFloat32Array(mesh.jointWeights, 'mesh.jointWeights'),
      elementSize: mesh.elementSize || 4,
      absPath: mesh.absPath,
      geomBindTransform: geomBindTransformMatrix,
      hasGeomBindTransform: mesh.hasGeomBindTransform || false
    };

    const meshKey = meshAbsPath || `__mesh_id_${i}`;
    allSkinnedMeshUSDData.set(meshKey, meshData);
    if (!skinnedMeshDataByName.has(meshName)) {
      skinnedMeshDataByName.set(meshName, meshData);
    } else {
      skinnedMeshDataByName.set(meshName, null);
    }

    if (!firstGeomBindTransform && geomBindTransformMatrix) {
      firstGeomBindTransform = geomBindTransformMatrix;
    }

    if (verbose) {
      logger.log(`Mesh ${i}: ${meshAbsPath || '(no absPath)'} (name: ${meshName})`);
      logger.log(`  - skel_id: ${mesh.skel_id}`);
      logger.log(`  - jointIndices: ${mesh.jointIndices ? mesh.jointIndices.length : 0} elements`);
      logger.log(`  - jointWeights: ${mesh.jointWeights ? mesh.jointWeights.length : 0} elements`);
      logger.log(`  - elementSize (influences per vertex): ${mesh.elementSize}`);
      logger.log(`  - hasGeomBindTransform: ${mesh.hasGeomBindTransform || false}`);
    }
  }

  return {
    numMeshes,
    hasSkinnedMeshData,
    firstGeomBindTransform,
    allSkinnedMeshUSDData,
    skinnedMeshDataByName
  };
}

