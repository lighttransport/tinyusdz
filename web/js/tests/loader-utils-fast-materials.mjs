#!/usr/bin/env node

import assert from 'node:assert/strict';
import * as THREE from 'three';
import { TinyUSDZLoaderUtils, TextureLoadingManager } from '../src/tinyusdz/TinyUSDZLoaderUtils.js';

globalThis.THREE = THREE;
globalThis.requestAnimationFrame = (fn) => setTimeout(() => fn(Date.now()), 0);

function makeMesh(materialId) {
  return {
    primName: `Mesh_${materialId}`,
    points: new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 0]),
    faceVertexIndices: new Uint32Array([0, 1, 2]),
    materialId,
  };
}

function makeMaterial(color) {
  return {
    hasUsdPreviewSurface: true,
    hasOpenPBR: false,
    surfaceShader: {
      diffuseColor: color,
      roughness: 0.5,
      metallic: 0,
    },
  };
}

{
  let getMaterialCalls = 0;
  let getMaterialWithFormatCalls = 0;
  const usdScene = {
    getMaterial(id) {
      getMaterialCalls++;
      return id === 2 ? makeMaterial([0, 0, 1]) : makeMaterial([1, 0, 0]);
    },
    getMaterialWithFormat(id) {
      getMaterialWithFormatCalls++;
      return { error: false, data: JSON.stringify(this.getMaterial(id)) };
    },
  };

  const options = {
    materialCache: new Map(),
    materialSignatureCache: new Map(),
    fastMaterials: true,
    fastMaterialMode: 'preview',
    preferredMaterialType: 'usdpreviewsurface',
    _debugState: {
      geometryMs: 0,
      materialMs: 0,
      materialSignatureMs: 0,
      materialConvertMs: 0,
      meshCreateMs: 0,
      materialCacheHits: 0,
      materialCacheMisses: 0,
      materialSignatureCacheHits: 0,
      materialSignatureCacheMisses: 0,
      singleMaterialMeshes: 0,
      multiMaterialMeshes: 0,
      subsetMaterialRefs: 0,
    },
  };

  const a = await TinyUSDZLoaderUtils.setupMesh(makeMesh(0), null, usdScene, options);
  const b = await TinyUSDZLoaderUtils.setupMesh(makeMesh(1), null, usdScene, options);
  const c = await TinyUSDZLoaderUtils.setupMesh(makeMesh(2), null, usdScene, options);

  assert.equal(getMaterialCalls, 3);
  assert.equal(getMaterialWithFormatCalls, 3);
  assert.equal(a.material, b.material, 'equivalent preview materials should share signature cache');
  assert.notEqual(a.material, c.material, 'different preview materials should not share signature cache');
  assert.equal(options._debugState.materialSignatureCacheHits, 1);
}

{
  const manager = new TextureLoadingManager();
  const usdScene = {};
  let loads = 0;
  const oldGetTextureFromUSD = TinyUSDZLoaderUtils.getTextureFromUSD;
  TinyUSDZLoaderUtils.getTextureFromUSD = async () => {
    loads++;
    return new THREE.Texture();
  };
  try {
    const m0 = new THREE.MeshPhysicalMaterial();
    const m1 = new THREE.MeshPhysicalMaterial();
    manager.queueTexture(m0, 'map', 7, usdScene);
    manager.queueTexture(m1, 'map', 7, usdScene);
    await manager.startLoading({ concurrency: 2 });
    assert.equal(loads, 1, 'duplicate texture IDs should share one load promise');
    assert.equal(m0.map, m1.map);
  } finally {
    TinyUSDZLoaderUtils.getTextureFromUSD = oldGetTextureFromUSD;
  }
}

console.log('OK loader-utils-fast-materials');
