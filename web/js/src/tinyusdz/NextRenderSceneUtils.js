import * as THREE from 'three';

export class NextTextureLoadingManager {
  constructor() {
    this.tasks = [];
    this.taskMap = new Map();
    this.promiseCache = new Map();
    this.total = 0;
    this.loaded = 0;
    this.failed = 0;
    this.aborted = false;
  }

  queueTexture(material, mapProperty, adapter, assetPath, colorRole = 'data') {
    if (!assetPath) return;
    const role = colorRole === 'color' ? 'color' : 'data';
    const key = `${role}:${assetPath}`;
    let task = this.taskMap.get(key);
    if (!task) {
      task = { adapter, assetPath, colorRole: role, bindings: [] };
      this.taskMap.set(key, task);
      this.tasks.push(task);
    }
    task.bindings.push({ material, mapProperty });
    this.total = this.tasks.length;
  }

  reset() {
    this.tasks = [];
    this.taskMap.clear();
    this.promiseCache.clear();
    this.total = 0;
    this.loaded = 0;
    this.failed = 0;
    this.aborted = false;
  }

  abort() {
    this.aborted = true;
  }

  getStatus() {
    return {
      loaded: this.loaded,
      failed: this.failed,
      total: this.total
    };
  }

  async startLoading({
    concurrency = 4,
    yieldInterval = 16,
    onTextureLoaded = null,
    onProgress = null
  } = {}) {
    this.aborted = false;
    let nextIndex = 0;
    const worker = async () => {
      while (!this.aborted && nextIndex < this.tasks.length) {
        const task = this.tasks[nextIndex++];
        try {
          const texture = await this.loadTexture(task);
          if (texture && !this.aborted) {
            for (const binding of task.bindings) {
              binding.material[binding.mapProperty] = texture;
              binding.material.needsUpdate = true;
              if (onTextureLoaded) onTextureLoaded(binding.material, texture, task);
            }
          }
          this.loaded++;
        } catch (error) {
          this.failed++;
          console.warn('[next-render] texture load failed', {
            assetPath: task.assetPath,
            mapProperty: task.bindings?.map((b) => b.mapProperty).join(',') || '',
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
    return this.getStatus();
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

export function isNextScene(usd) {
  return usd && usd.__backend === 'next';
}

export function textureColorRoleForMap(mapProperty) {
  return (mapProperty === 'map' || mapProperty === 'emissiveMap') ? 'color' : 'data';
}

export async function loadNextArchiveTexture(adapter, assetPath, role = 'data') {
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

export function createNextMaterial(entry, adapter, textureManager, skipTextures) {
  const src = entry.material || {};
  const paths = entry.texturePaths || {};
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

export function nextMaterialCacheKey(entry) {
  if (Number.isFinite(entry?.materialId) && entry.materialId >= 0) {
    return `id:${entry.materialId}`;
  }
  if (entry?.materialKey) return `key:${entry.materialKey}`;
  return `fallback:${JSON.stringify(entry?.material || {})}:${JSON.stringify(entry?.texturePaths || {})}`;
}

export function buildNextThreeNode(adapter, { skipTextures = true, lazyTextures = false } = {}) {
  const group = new THREE.Group();
  group.name = adapter.filename || 'next-scene';
  const textureManager = (lazyTextures && !skipTextures) ? new NextTextureLoadingManager() : null;
  const materialCache = new Map();
  const getMaterial = (entry) => {
    const key = nextMaterialCacheKey(entry);
    let material = materialCache.get(key);
    if (!material) {
      material = createNextMaterial(entry, adapter, textureManager, skipTextures);
      materialCache.set(key, material);
    }
    return material;
  };
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
    let material = getMaterial(mesh);
    if (Array.isArray(mesh.materials) && mesh.materials.length &&
        Array.isArray(mesh.submeshes) && mesh.submeshes.length) {
      const materialEntries = mesh.materials.map((entry) => ({
        material: entry.material || mesh.material,
        texturePaths: entry.texturePaths || {},
        materialId: Number.isFinite(entry.materialId) ? entry.materialId : -1,
        materialKey: entry.materialKey || mesh.materialKey
      }));
      materialEntries.push({
        material: mesh.material,
        texturePaths: mesh.texturePaths,
        materialId: Number.isFinite(mesh.materialId) ? mesh.materialId : -1,
        materialKey: mesh.materialKey
      });
      const fallbackIndex = materialEntries.length - 1;
      material = materialEntries.map((entry) => getMaterial(entry));
      const covered = [];
      for (const part of mesh.submeshes) {
        const start = Math.max(0, part.start | 0);
        const count = Math.max(0, part.count | 0);
        if (count <= 0) continue;
        const materialIndex = (part.materialIndex >= 0 && part.materialIndex < material.length)
          ? part.materialIndex : fallbackIndex;
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

  return { node: group, textureManager };
}

export function readNextSceneMeta(usd) {
  const md = (usd && typeof usd.getSceneMetadata === 'function') ? usd.getSceneMetadata() : {};
  return {
    upAxis: md.upAxis || 'Y',
    metersPerUnit: (typeof md.metersPerUnit === 'number' && md.metersPerUnit > 0)
      ? md.metersPerUnit : 1.0
  };
}

export function nextCountsFromScene(usd) {
  const stats = usd && typeof usd.getStats === 'function' ? usd.getStats() : null;
  return {
    meshes: usd && usd.numMeshes ? usd.numMeshes() : 0,
    materials: usd && usd.numMaterials ? usd.numMaterials() : 0,
    textures: usd && usd.numTextures ? usd.numTextures() : 0,
    stats
  };
}
