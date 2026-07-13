import * as THREE from 'three';

export function defaultTextureConcurrency() {
  const cores = (typeof navigator !== 'undefined' && Number.isFinite(navigator.hardwareConcurrency))
    ? navigator.hardwareConcurrency
    : 8;
  return Math.max(4, Math.min(16, cores || 8));
}

export class NextTextureLoadingManager {
  constructor() {
    this.tasks = [];
    this.taskMap = new Map();
    this.promiseCache = new Map();
    this.total = 0;
    this.loaded = 0;
    this.failed = 0;
    this.aborted = false;
    this.isLoading = false;
    this.pendingReleaseAdapters = new Set();
  }

  queueTexture(
    material, mapProperty, adapter, assetPath, colorRole = 'data', sampler = null,
    materialXOps = null) {
    if (!assetPath) return;
    const role = colorRole === 'color' ? 'color' : 'data';
    const wrapS = nextTextureWrapMode(sampler?.wrapS);
    const wrapT = nextTextureWrapMode(sampler?.wrapT);
    const opsKey = materialXTextureOpsKey(materialXOps);
    const key = `${role}:${wrapS}:${wrapT}:${assetPath}:${opsKey}`;
    let task = this.taskMap.get(key);
    if (!task) {
      task = {
        adapter, assetPath, colorRole: role, wrapS, wrapT,
        materialXOps: materialXOps || [], bindings: []
      };
      this.taskMap.set(key, task);
      this.tasks.push(task);
    }
    task.bindings.push({ material, mapProperty });
    this.total = this.tasks.length;
  }

  reset() {
    const adapters = new Set();
    for (const task of this.tasks) {
      if (task.adapter && typeof task.adapter.releaseArchiveTextureBytes === 'function') {
        adapters.add(task.adapter);
      }
    }
    this.tasks = [];
    this.taskMap.clear();
    this.promiseCache.clear();
    this.total = 0;
    this.loaded = 0;
    this.failed = 0;
    this.aborted = true;
    this.releaseAdapters(adapters);
  }

  abort() {
    this.aborted = true;
  }

  releaseAdapters(adapters) {
    if (!adapters || adapters.size === 0) return;
    if (this.isLoading) {
      for (const adapter of adapters) this.pendingReleaseAdapters.add(adapter);
      return;
    }
    for (const adapter of adapters) adapter.releaseArchiveTextureBytes();
  }

  getStatus() {
    const completed = this.loaded + this.failed;
    return {
      loaded: this.loaded,
      failed: this.failed,
      total: this.total,
      pending: Math.max(0, this.total - completed),
      percentage: this.total > 0 ? (completed / this.total) * 100 : 100,
      isComplete: this.total === 0 || completed >= this.total
    };
  }

  async startLoading({
    concurrency = defaultTextureConcurrency(),
    yieldInterval = 16,
    onTextureLoaded = null,
    onProgress = null
  } = {}) {
    if (this.isLoading) {
      throw new Error('texture loading is already running');
    }
    this.isLoading = true;
    this.aborted = false;
    if (onProgress) {
      onProgress({
        ...this.getStatus(),
        percentage: this.total > 0 ? 0 : 100,
        isStart: true,
        isComplete: this.total === 0
      });
    }
    let nextIndex = 0;
    let lastYieldTime = performance.now();
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
          const unsupported = error?.name === 'UnsupportedTextureFormatError';
          console.warn(unsupported ? '[next-render] unsupported texture format' : '[next-render] texture load failed', JSON.stringify({
            assetPath: task.assetPath,
            mapProperty: task.bindings?.map((b) => b.mapProperty).join(',') || '',
            error: error?.message || String(error),
            errorName: error?.name || undefined
          }));
        }
        if (onProgress) {
          onProgress({
            ...this.getStatus(),
            currentTexture: task.assetPath,
            mapProperty: task.bindings?.map((b) => b.mapProperty).join(',') || ''
          });
        }
        const now = performance.now();
        if (yieldInterval > 0 && now - lastYieldTime >= yieldInterval) {
          lastYieldTime = now;
          await new Promise((resolve) => setTimeout(resolve, 0));
        }
      }
    };
    try {
      await Promise.all(Array.from({ length: Math.max(1, concurrency) }, worker));
      return this.getStatus();
    } finally {
      this.isLoading = false;
      const adapters = this.pendingReleaseAdapters;
      this.pendingReleaseAdapters = new Set();
      this.releaseAdapters(adapters);
    }
  }

  async loadTexture(task) {
    const role = task.colorRole === 'color' ? 'color' : 'data';
    const sourceKey = `${role}:${task.wrapS}:${task.wrapT}:${task.assetPath}`;
    if (!this.promiseCache.has(sourceKey)) {
      this.promiseCache.set(
        sourceKey, loadNextArchiveTexture(task.adapter, task.assetPath, role, task));
    }
    const opsKey = materialXTextureOpsKey(task.materialXOps);
    if (!opsKey) return this.promiseCache.get(sourceKey);

    const instanceKey = `${sourceKey}:${opsKey}`;
    if (!this.promiseCache.has(instanceKey)) {
      this.promiseCache.set(instanceKey, this.promiseCache.get(sourceKey).then((source) => {
        const texture = source.clone();
        // Texture.clone() may share its Source with the original. Baking by
        // assigning texture.image would then rewrite every material using the
        // deduplicated archive image (e.g. direct, gamma, and inverse-gamma
        // variants of one PNG). Give the transformed instance its own Source.
        texture.source = new THREE.Source(source.image);
        bakeMaterialXTextureOps(texture, task.materialXOps);
        return texture;
      }));
    }
    return this.promiseCache.get(instanceKey);
  }
}

export function isNextScene(usd) {
  return usd && usd.__backend === 'next';
}

export function textureColorRoleForMap(mapProperty) {
  return (mapProperty === 'map' || mapProperty === 'emissiveMap') ? 'color' : 'data';
}

export function nextTextureWrapMode(wrap) {
  if (wrap === THREE.RepeatWrapping || wrap === THREE.MirroredRepeatWrapping ||
      wrap === THREE.ClampToEdgeWrapping) {
    return wrap;
  }
  switch (String(wrap || '').toLowerCase()) {
    case 'repeat':
    case 'tile':
    case 'tileforever':
      return THREE.RepeatWrapping;
    case 'mirror':
    case 'mirroredrepeat':
      return THREE.MirroredRepeatWrapping;
    case 'black':
    case 'clamp':
    case 'clamp_to_edge':
    case 'clamp_to_border':
    case 'usemetadata':
    default:
      // WebGL has no portable border-color sampler. Clamp matches the legacy
      // renderer and preserves transparent image borders for USD `black`.
      return THREE.ClampToEdgeWrapping;
  }
}

/// Effective color role for a texture: an authored colorspace (colorSpace
/// asset metadata or inputs:sourceColorSpace) wins over the map-role
/// default; "auto"/unknown keeps the role-based heuristic (equivalent to
/// the UsdPreviewSurface auto rule for typical 8-bit web textures).
export function textureColorRole(mapProperty, authoredColorSpace) {
  const authored = String(authoredColorSpace || '').toLowerCase();
  if (authored === 'srgb' || authored === 'srgb_texture' || authored === 'srgb_displayp3') {
    return 'color';
  }
  if (authored === 'raw' || authored === 'linear' || authored === 'lin_srgb' ||
      authored === 'lin_rec709' || authored === 'scene-linear rec.709-srgb' ||
      authored === 'lin_rec2020' || authored === 'lin_displayp3' ||
      authored === 'acescg') {
    return 'data';
  }
  return textureColorRoleForMap(mapProperty);
}

function materialXCategory(node) {
  return String(node?.category || '')
    .replace(/_(color[234]|vector[234]|float|integer|boolean)$/, '');
}

function materialXTextureOpsKey(ops) {
  if (!Array.isArray(ops) || ops.length === 0) return '';
  return ops.map((op) => {
    const values = {};
    for (const input of (op.node?.inputs || [])) {
      if (input.value !== undefined) values[input.name] = input.value;
    }
    return `${op.category}:${JSON.stringify(values)}`;
  }).join('|');
}

function materialXOpsNeedBake(ops) {
  if (!Array.isArray(ops) || ops.length === 0) return false;
  const hasCombine = ops.some((op) => String(op.category || '').startsWith('combine'));
  return ops.some((op) =>
    op.category === 'power' || op.category === 'multiply' ||
    op.category === 'add' || op.category === 'invert' ||
    (op.category === 'extract' && !hasCombine));
}

function bakeMaterialXTextureOps(texture, ops) {
  if (!texture?.image || !materialXOpsNeedBake(ops)) return;
  const image = texture.image;
  const canvas = document.createElement('canvas');
  canvas.width = image.width;
  canvas.height = image.height;
  const context = canvas.getContext('2d');
  if (!context) return;
  context.drawImage(image, 0, 0);
  const imageData = context.getImageData(0, 0, canvas.width, canvas.height);
  const data = imageData.data;
  const hasCombine = ops.some((op) => String(op.category || '').startsWith('combine'));

  // Graph traversal records output-to-image order. Pixel operations execute in
  // the authored image-to-output order.
  for (const op of [...ops].reverse()) {
    const inputValue = (name) =>
      (op.node?.inputs || []).find((input) => input.name === name)?.value;
    const components = (value, fallback = 0) => {
      if (Array.isArray(value)) {
        return [value[0] ?? fallback, value[1] ?? value[0] ?? fallback,
          value[2] ?? value[0] ?? fallback];
      }
      const scalar = value ?? fallback;
      return [scalar, scalar, scalar];
    };
    if (op.category === 'power') {
      const exponent = inputValue('in2');
      if (exponent === undefined) continue;
      const e = components(exponent, 1);
      for (let i = 0; i < data.length; i += 4) {
        data[i] = Math.round(Math.pow(data[i] / 255, e[0]) * 255);
        data[i + 1] = Math.round(Math.pow(data[i + 1] / 255, e[1]) * 255);
        data[i + 2] = Math.round(Math.pow(data[i + 2] / 255, e[2]) * 255);
      }
    } else if (op.category === 'multiply') {
      const factor = inputValue('in2');
      if (factor === undefined) continue;
      const f = components(factor, 1);
      for (let i = 0; i < data.length; i += 4) {
        data[i] = Math.min(255, Math.round(data[i] * f[0]));
        data[i + 1] = Math.min(255, Math.round(data[i + 1] * f[1]));
        data[i + 2] = Math.min(255, Math.round(data[i + 2] * f[2]));
      }
    } else if (op.category === 'add') {
      const offset = inputValue('in2');
      if (offset === undefined) continue;
      const o = components(offset);
      for (let i = 0; i < data.length; i += 4) {
        data[i] = Math.min(255, Math.max(0, Math.round(data[i] + o[0] * 255)));
        data[i + 1] = Math.min(255, Math.max(0, Math.round(data[i + 1] + o[1] * 255)));
        data[i + 2] = Math.min(255, Math.max(0, Math.round(data[i + 2] + o[2] * 255)));
      }
    } else if (op.category === 'invert') {
      for (let i = 0; i < data.length; i += 4) {
        data[i] = 255 - data[i];
        data[i + 1] = 255 - data[i + 1];
        data[i + 2] = 255 - data[i + 2];
      }
    } else if (op.category === 'extract' && !hasCombine) {
      const channel = Math.max(0, Math.min(3, Number(inputValue('index') ?? 0)));
      for (let i = 0; i < data.length; i += 4) {
        const value = data[i + channel];
        data[i] = value;
        data[i + 1] = value;
        data[i + 2] = value;
        data[i + 3] = 255;
      }
    }
  }
  context.putImageData(imageData, 0, 0);
  texture.image = canvas;
  texture.needsUpdate = true;
}

// Resolve the image and pixel-operation chain feeding one OpenPBR input.
// This mirrors the legacy demo's graph-aware texture path, but lives in the
// shared next scene builder so every next consumer gets identical behavior.
export function materialXTextureSpecForParam(nodeGraphData, paramName) {
  if (!nodeGraphData || !paramName) return null;
  const graph = nodeGraphData.nodegraph || nodeGraphData;
  const nodes = graph.nodes || [];
  const outputs = graph.outputs || [];
  const connections = nodeGraphData.connections || graph.connections || [];
  const nodeMap = new Map(nodes.map((node) => [node.name, node]));
  const images = new Map();
  for (const node of nodes) {
    const category = materialXCategory(node);
    if (category !== 'image' && category !== 'tiledimage') continue;
    const file = (node.inputs || []).find((input) => input.name === 'file');
    if (file?.value) {
      images.set(node.name, {
        filename: file.value,
        colorspace: file.colorspace || file.colorSpace || ''
      });
    }
  }
  if (images.size === 0) return null;

  const targetOutputs = new Set(connections
    .filter((connection) => connection.input === paramName)
    .map((connection) => connection.output));
  // If the graph has an explicit surface-connection table, absence of this
  // parameter means it is not graph-driven. Falling back to the first graph
  // output can otherwise apply an opacity extract to the base-color map.
  if (connections.length > 0 && targetOutputs.size === 0) return null;
  const trace = (nodeName, ops = [], visited = new Set()) => {
    if (!nodeName || visited.has(nodeName)) return null;
    if (images.has(nodeName)) return { ...images.get(nodeName), ops };
    const node = nodeMap.get(nodeName);
    if (!node) return null;
    const nextVisited = new Set(visited);
    nextVisited.add(nodeName);
    const category = materialXCategory(node);
    const passThrough = category.startsWith('convert') || category.startsWith('texcoord') ||
      category.startsWith('normalmap') || category.startsWith('heighttonormal');
    const nextOps = passThrough ? ops : [...ops, { name: nodeName, category, node }];
    for (const input of (node.inputs || [])) {
      if (!input.nodename) continue;
      const found = trace(input.nodename, nextOps, nextVisited);
      if (found) return found;
    }
    return null;
  };

  for (const output of outputs) {
    if (targetOutputs.size > 0 && !targetOutputs.has(output.name)) continue;
    const found = trace(output.nodename);
    if (found) return found;
  }
  return null;
}

function isUnsupportedBrowserTexturePath(assetPath) {
  return /\.(psd|tga|dds|ktx2?)($|[?#])/i.test(String(assetPath || ''));
}

function browserTextureMimeType(assetPath) {
  const path = String(assetPath || '').split('?')[0].split('#')[0].toLowerCase();
  if (path.endsWith('.jpg') || path.endsWith('.jpeg')) return 'image/jpeg';
  if (path.endsWith('.png')) return 'image/png';
  if (path.endsWith('.webp')) return 'image/webp';
  if (path.endsWith('.gif')) return 'image/gif';
  if (path.endsWith('.bmp')) return 'image/bmp';
  if (path.endsWith('.tif') || path.endsWith('.tiff')) return 'image/tiff';
  return '';
}

export async function loadNextArchiveTexture(adapter, assetPath, role = 'data', sampler = null) {
  if (isUnsupportedBrowserTexturePath(assetPath)) {
    const error = new Error(`texture format is not supported by this browser demo: ${assetPath}`);
    error.name = 'UnsupportedTextureFormatError';
    throw error;
  }
  const udim = typeof adapter?.getArchiveUDIMTiles === 'function'
    ? adapter.getArchiveUDIMTiles(assetPath)
    : null;
  if (udim?.tiles?.length) {
    const images = await Promise.all(udim.tiles.map(async (tile) => {
      const mimeType = browserTextureMimeType(tile.path);
      const blob = new Blob([tile.bytes], mimeType ? { type: mimeType } : undefined);
      const blobUrl = URL.createObjectURL(blob);
      try {
        const image = await new THREE.ImageLoader().loadAsync(blobUrl);
        return { ...tile, image };
      } finally {
        URL.revokeObjectURL(blobUrl);
      }
    }));
    const tileWidth = Math.max(...images.map((tile) => tile.image.width || 1));
    const tileHeight = Math.max(...images.map((tile) => tile.image.height || 1));
    const width = tileWidth * udim.columns;
    const height = tileHeight * udim.rows;
    const canvas = typeof OffscreenCanvas === 'function'
      ? new OffscreenCanvas(width, height)
      : Object.assign(document.createElement('canvas'), { width, height });
    const context = canvas.getContext('2d');
    if (!context) throw new Error(`could not create UDIM atlas canvas: ${assetPath}`);
    context.clearRect(0, 0, width, height);
    for (const tile of images) {
      const y = (udim.rows - 1 - tile.v) * tileHeight;
      context.drawImage(tile.image, tile.u * tileWidth, y, tileWidth, tileHeight);
    }
    const texture = new THREE.CanvasTexture(canvas);
    texture.name = `${assetPath} [${images.map((tile) => tile.id).join(',')}]`;
    texture.colorSpace = role === 'color' ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    texture.wrapS = nextTextureWrapMode(sampler?.wrapS);
    texture.wrapT = nextTextureWrapMode(sampler?.wrapT);
    texture.flipY = true;
    texture.needsUpdate = true;
    return texture;
  }

  const bytes = adapter.getArchiveTextureBytes(assetPath);
  if (!bytes) {
    throw new Error(`texture asset not found in archive: ${assetPath}`);
  }
  const mimeType = browserTextureMimeType(assetPath);
  const blob = new Blob([bytes], mimeType ? { type: mimeType } : undefined);
  const blobUrl = URL.createObjectURL(blob);
  try {
    const texture = await new THREE.TextureLoader().loadAsync(blobUrl);
    texture.colorSpace = role === 'color' ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    texture.wrapS = nextTextureWrapMode(sampler?.wrapS);
    texture.wrapT = nextTextureWrapMode(sampler?.wrapT);
    texture.flipY = true;
    texture.needsUpdate = true;
    return texture;
  } finally {
    URL.revokeObjectURL(blobUrl);
  }
}

function parseJsonObject(value) {
  if (!value) return null;
  if (typeof value === 'object') return value;
  if (typeof value !== 'string') return null;
  try {
    const parsed = JSON.parse(value);
    return parsed && typeof parsed === 'object' ? parsed : null;
  } catch {
    return null;
  }
}

function normalizeParam(param, fallbackValue = undefined) {
  if (param && typeof param === 'object' && !Array.isArray(param)) {
    const out = {};
    if (param.value !== undefined) out.value = param.value;
    if (param.texture !== undefined && param.texture !== '') out.texture = param.texture;
    if (param.texturePath !== undefined && param.texturePath !== '') out.texturePath = param.texturePath;
    if (Number.isFinite(param.textureId)) out.textureId = param.textureId;
    if (param.colorspace !== undefined && param.colorspace !== '') out.colorspace = param.colorspace;
    if (Object.keys(out).length) return out;
  }
  if (param !== undefined && param !== null) return { value: param };
  if (fallbackValue !== undefined && fallbackValue !== null) return { value: fallbackValue };
  return undefined;
}

function paramValue(param, fallbackValue = undefined) {
  if (param && typeof param === 'object' && !Array.isArray(param) && param.value !== undefined) {
    return param.value;
  }
  if (param !== undefined && param !== null) return param;
  return fallbackValue;
}

function firstParam(source, keys, fallbackValue = undefined) {
  for (const key of keys) {
    if (source && source[key] !== undefined && source[key] !== null) {
      return normalizeParam(source[key], fallbackValue);
    }
  }
  return normalizeParam(null, fallbackValue);
}

function attachOpenPBRParam(openPBR, sectionName, key, param) {
  if (!param) return;
  if (!openPBR[sectionName]) openPBR[sectionName] = {};
  openPBR[sectionName][key] = param;
  openPBR[key] = param;
}

function texturePathParam(value, texturePath) {
  const param = normalizeParam(null, value) || (texturePath ? {} : undefined);
  if (param && texturePath) {
    param.texturePath = texturePath;
    param.texture = texturePath;
    param.textureId = Number.isFinite(param.textureId) ? param.textureId : -1;
  }
  return param;
}

function normalizeNodeGraphJson(materialRecord, parsedMaterial) {
  const openPBR = parsedMaterial?.openPBR || {};
  const graphCandidates = [
    openPBR.nodeGraph,
    openPBR.nodegraph,
    openPBR.nodegraphJson,
    openPBR.nodeGraphJson,
    materialRecord?.openPBRNodeGraphJson
  ];
  for (const candidate of graphCandidates) {
    const parsed = parseJsonObject(candidate);
    if (parsed) return parsed;
    if (candidate && typeof candidate === 'object') return candidate;
  }
  return null;
}

export function normalizeNextMaterialData(materialRecord = {}, texturePaths = {}) {
  const parsed = parseJsonObject(materialRecord.materialXJson) || {};
  const parsedPreview = parsed.previewSurface || {};
  const parsedOpenPBR = parsed.openPBR || {};
  const shaderType = materialRecord.shaderType || parsed.shaderType || '';
  const name = materialRecord.name || materialRecord.key || parsed.name || '';
  const primPath = materialRecord.primPath || parsed.primPath || '';
  const baseColor = materialRecord.baseColor || paramValue(parsedOpenPBR.baseColor) ||
    paramValue(parsedPreview.diffuseColor) || [0.8, 0.8, 0.8];
  const roughness = materialRecord.roughness ?? paramValue(parsedOpenPBR.baseRoughness) ??
    paramValue(parsedPreview.roughness) ?? 0.5;
  const metalness = materialRecord.metallic ?? paramValue(parsedOpenPBR.baseMetalness) ??
    paramValue(parsedPreview.metallic) ?? 0.0;
  const opacity = materialRecord.opacity ?? paramValue(parsedOpenPBR.opacity) ??
    paramValue(parsedPreview.opacity) ?? 1.0;
  const emissive = materialRecord.emissive || paramValue(parsedOpenPBR.emissionColor) ||
    paramValue(parsedPreview.emissiveColor) || [0.0, 0.0, 0.0];

  const surfaceShader = {
    diffuseColor: texturePathParam(baseColor, texturePaths.baseColor),
    metallic: texturePathParam(metalness, texturePaths.metallic),
    roughness: texturePathParam(roughness, texturePaths.roughness),
    opacity: texturePathParam(opacity, texturePaths.opacity),
    emissiveColor: texturePathParam(emissive, texturePaths.emissive),
    normal: texturePathParam(undefined, texturePaths.normal)
  };

  const openPBR = { type: 'OpenPBR' };
  attachOpenPBRParam(openPBR, 'base', 'base_color',
    firstParam(parsedOpenPBR, ['base_color', 'baseColor'], baseColor));
  attachOpenPBRParam(openPBR, 'base', 'base_weight',
    firstParam(parsedOpenPBR, ['base_weight', 'baseWeight'], 1.0));
  attachOpenPBRParam(openPBR, 'base', 'base_metalness',
    firstParam(parsedOpenPBR, ['base_metalness', 'baseMetalness'], metalness));
  attachOpenPBRParam(openPBR, 'base', 'base_roughness',
    firstParam(parsedOpenPBR, ['base_roughness', 'baseRoughness'], roughness));
  attachOpenPBRParam(openPBR, 'specular', 'specular_roughness',
    firstParam(parsedOpenPBR, ['specular_roughness', 'baseRoughness'], roughness));
  attachOpenPBRParam(openPBR, 'specular', 'specular_weight',
    firstParam(parsedOpenPBR, ['specular_weight', 'specularWeight'], 1.0));
  attachOpenPBRParam(openPBR, 'specular', 'specular_color',
    firstParam(parsedOpenPBR, ['specular_color', 'specularColor'], [1.0, 1.0, 1.0]));
  attachOpenPBRParam(openPBR, 'emission', 'emission_color',
    firstParam(parsedOpenPBR, ['emission_color', 'emissionColor'], emissive));
  attachOpenPBRParam(openPBR, 'emission', 'emission_luminance',
    firstParam(parsedOpenPBR, ['emission_luminance', 'emissionLuminance'], 0.0));
  attachOpenPBRParam(openPBR, 'geometry', 'geometry_opacity',
    firstParam(parsedOpenPBR, ['geometry_opacity', 'opacity'], opacity));
  attachOpenPBRParam(openPBR, 'geometry', 'normal',
    firstParam(parsedOpenPBR, ['normal', 'geometry_normal'], undefined));
  attachOpenPBRParam(openPBR, 'geometry', 'geometry_normal', openPBR.normal);

  if (texturePaths.baseColor) {
    openPBR.base_color.texturePath = texturePaths.baseColor;
    openPBR.base_color.texture = texturePaths.baseColor;
    openPBR.base_color.textureId = -1;
  }
  if (texturePaths.roughness) {
    openPBR.specular_roughness.texturePath = texturePaths.roughness;
    openPBR.specular_roughness.texture = texturePaths.roughness;
    openPBR.specular_roughness.textureId = -1;
  }
  if (texturePaths.metallic) {
    openPBR.base_metalness.texturePath = texturePaths.metallic;
    openPBR.base_metalness.texture = texturePaths.metallic;
    openPBR.base_metalness.textureId = -1;
  }
  if (texturePaths.emissive) {
    openPBR.emission_color.texturePath = texturePaths.emissive;
    openPBR.emission_color.texture = texturePaths.emissive;
    openPBR.emission_color.textureId = -1;
  }
  if (texturePaths.normal) {
    if (!openPBR.normal) {
      attachOpenPBRParam(openPBR, 'geometry', 'normal', {
        texturePath: texturePaths.normal,
        texture: texturePaths.normal,
        textureId: -1
      });
    }
    openPBR.normal.texturePath = texturePaths.normal;
    openPBR.normal.texture = texturePaths.normal;
    openPBR.normal.textureId = -1;
    openPBR.geometry_normal = openPBR.normal;
    openPBR.geometry.geometry_normal = openPBR.normal;
  }

  const nodeGraph = normalizeNodeGraphJson(materialRecord, parsed);
  if (nodeGraph) openPBR.nodeGraph = nodeGraph;

  return {
    name,
    materialName: name,
    primPath,
    shaderType,
    hasOpenPBR: shaderType === 'OpenPBR' || !!parsed.openPBR || !!nodeGraph,
    hasUsdPreviewSurface: shaderType === 'PreviewSurface' || !!parsed.previewSurface || !!materialRecord.baseColor,
    materialXConfig: parsed.materialXConfig || materialRecord.materialXConfig || {},
    surfaceShader,
    openPBR,
    openPBRShader: openPBR,
    texturePaths: { ...texturePaths },
    textureMetadata: materialRecord.textureMetadata || {},
    materialXJson: materialRecord.materialXJson || '',
    openPBRNodeGraphJson: materialRecord.openPBRNodeGraphJson || '',
    __nextMaterial: true
  };
}

export function createNextMaterial(entry, adapter, textureManager, skipTextures) {
  const src = entry.material || {};
  const paths = entry.texturePaths || {};
  const hasOpacityMap = !!paths.opacity && !skipTextures;
  const authoredAlphaTest = (src.opacityThreshold ?? -1) > 0 ? src.opacityThreshold : 0;
  const material = new THREE.MeshPhysicalMaterial({
    // A connected shader input replaces its fallback value; it is not a tint.
    color: paths.baseColor && !skipTextures ? new THREE.Color(1, 1, 1) :
      new THREE.Color(src.baseColor?.[0] ?? 0.8, src.baseColor?.[1] ?? 0.8,
        src.baseColor?.[2] ?? 0.8),
    metalness: src.metallic ?? 0,
    roughness: src.roughness ?? 0.5,
    emissive: new THREE.Color(src.emissive?.[0] ?? 0, src.emissive?.[1] ?? 0, src.emissive?.[2] ?? 0),
    transparent: (src.opacity ?? 1) < 1 || hasOpacityMap,
    opacity: src.opacity ?? 1,
    // Discard effectively-zero opacity before PBR lighting. Large VFX cards
    // otherwise shade millions of invisible fragments and can dominate frame
    // time even though their final composited contribution is zero.
    alphaTest: authoredAlphaTest || (hasOpacityMap ? 1 / 255 : 0)
  });
  material.userData.nextTexturePaths = paths;
  material.userData.nextTextureMetadata = src.textureMetadata || entry.textureMetadata || {};
  material.userData.nextMaterialXJson = src.materialXJson || '';
  material.userData.nextOpenPBRNodeGraphJson = src.openPBRNodeGraphJson || '';
  material.userData.nextMaterial = {
    id: Number.isFinite(entry.materialId) ? entry.materialId : (src.id ?? -1),
    key: entry.materialKey || src.key || '',
    primPath: src.primPath || '',
    baseColor: src.baseColor || null,
    metallic: src.metallic ?? null,
    roughness: src.roughness ?? null,
    opacity: src.opacity ?? null
  };
  const rawData = normalizeNextMaterialData(src, paths);
  material.userData.rawData = rawData;
  material.userData.typeInfo = {
    hasOpenPBR: !!rawData.hasOpenPBR,
    hasUsdPreviewSurface: !!rawData.hasUsdPreviewSurface
  };
  material.userData.typeString = rawData.hasOpenPBR
    ? (rawData.hasUsdPreviewSurface ? 'OpenPBR + PreviewSurface' : 'OpenPBR')
    : (rawData.hasUsdPreviewSurface ? 'PreviewSurface' : 'Unknown');

  // Authored colorspace lives in textureMetadata keyed by texture role.
  const metadataRoleForMap = {
    map: 'baseColor',
    normalMap: 'normal',
    roughnessMap: 'roughness',
    metalnessMap: 'metallic',
    aoMap: 'occlusion',
    emissiveMap: 'emissive',
    alphaMap: 'opacity'
  };
  const queue = (mapProperty, assetPath) => {
    if (!assetPath || skipTextures || !textureManager) return;
    const meta = material.userData.nextTextureMetadata?.[metadataRoleForMap[mapProperty]];
    const graphParamForMap = {
      map: 'base_color',
      normalMap: 'geometry_normal',
      roughnessMap: 'specular_roughness',
      metalnessMap: 'base_metalness',
      emissiveMap: 'emission_color',
      alphaMap: 'geometry_opacity'
    };
    let graphSpec = materialXTextureSpecForParam(
      rawData.openPBR?.nodeGraph, graphParamForMap[mapProperty]);
    if (!graphSpec && mapProperty === 'normalMap') {
      graphSpec = materialXTextureSpecForParam(rawData.openPBR?.nodeGraph, 'normal');
    }
    const openPBRParam = rawData.openPBR?.[graphParamForMap[mapProperty]] ||
      (mapProperty === 'normalMap' ? rawData.openPBR?.normal : null);
    // The next node-graph JSON intentionally keeps the graph structural and
    // may omit SdfAssetPath metadata. Its reconstructed OpenPBR parameter
    // retains that colorspace, so consult it before PreviewSurface metadata.
    const authored = graphSpec?.colorspace || openPBRParam?.colorspace ||
      meta?.sourceColorSpace || meta?.colorspace || '';
    textureManager.queueTexture(
      material, mapProperty, adapter, assetPath,
      textureColorRole(mapProperty, authored), meta, graphSpec?.ops);
  };
  queue('map', paths.baseColor);
  queue('normalMap', paths.normal);
  queue('roughnessMap', paths.roughness);
  queue('metalnessMap', paths.metallic);
  queue('aoMap', paths.occlusion);
  queue('emissiveMap', paths.emissive);
  // When opacity and base color use the same plain RGBA image, Three.js's map
  // already carries its alpha. A MaterialX opacity connection can instead
  // extract/transform an RGB channel from that same image (laser beams and
  // projected-shadow cards); those still require a separately baked alphaMap.
  const opacityGraphSpec = materialXTextureSpecForParam(
    rawData.openPBR?.nodeGraph, 'geometry_opacity');
  if (paths.opacity !== paths.baseColor || opacityGraphSpec) {
    queue('alphaMap', paths.opacity);
  }
  return material;
}

export function nextMaterialCacheKey(entry) {
  if (Number.isFinite(entry?.materialId) && entry.materialId >= 0) {
    return `id:${entry.materialId}`;
  }
  if (entry?.materialKey) return `key:${entry.materialKey}`;
  return `fallback:${JSON.stringify(entry?.material || {})}:${JSON.stringify(entry?.texturePaths || {})}`;
}

// Three.js submits one draw per BufferGeometry group. USD GeomSubsets are
// arbitrary face sets, so representing alternating subset faces as contiguous
// ranges can create tens of thousands of groups. Reorder only the triangle
// index buffer by material (vertex attributes stay untouched) and emit one
// contiguous group per material.
export function compactMaterialGroups(
  indices, parts, materialCount, fallbackIndex, vertexCount = 0) {
  const elementCount = indices?.length || Math.floor(vertexCount / 3) * 3;
  if (!elementCount || !Array.isArray(parts) || parts.length === 0 || materialCount <= 0) {
    return null;
  }
  const triangleCount = Math.floor(elementCount / 3);
  const triangleMaterial = new Int32Array(triangleCount);
  triangleMaterial.fill(fallbackIndex);
  for (const part of parts) {
    const materialIndex = (part.materialIndex >= 0 && part.materialIndex < materialCount)
      ? part.materialIndex : fallbackIndex;
    const first = Math.max(0, Math.floor((part.start || 0) / 3));
    const end = Math.min(triangleCount,
      Math.ceil(((part.start || 0) + Math.max(0, part.count || 0)) / 3));
    for (let triangle = first; triangle < end; ++triangle) {
      triangleMaterial[triangle] = materialIndex;
    }
  }

  const counts = new Uint32Array(materialCount);
  for (let triangle = 0; triangle < triangleCount; ++triangle) {
    counts[triangleMaterial[triangle]] += 3;
  }
  const offsets = new Uint32Array(materialCount);
  const cursors = new Uint32Array(materialCount);
  let offset = 0;
  for (let materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
    offsets[materialIndex] = offset;
    cursors[materialIndex] = offset;
    offset += counts[materialIndex];
  }
  const IndexType = indices?.constructor || Uint32Array;
  const reordered = new IndexType(elementCount);
  for (let triangle = 0; triangle < triangleCount; ++triangle) {
    const materialIndex = triangleMaterial[triangle];
    const dst = cursors[materialIndex];
    const src = triangle * 3;
    reordered[dst] = indices ? indices[src] : src;
    reordered[dst + 1] = indices ? indices[src + 1] : src + 1;
    reordered[dst + 2] = indices ? indices[src + 2] : src + 2;
    cursors[materialIndex] += 3;
  }
  const groups = [];
  for (let materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
    if (counts[materialIndex] > 0) {
      groups.push({ start: offsets[materialIndex], count: counts[materialIndex], materialIndex });
    }
  }
  return { indices: reordered, groups };
}

export function buildNextThreeNode(adapter, {
  skipTextures = true,
  lazyTextures = false,
  onProgress = null,
  progressInterval = 25,
  releaseBuildData = true,
  showCurves = true
} = {}) {
  const group = new THREE.Group();
  group.name = adapter.filename || 'next-scene';
  const textureManager = (lazyTextures && !skipTextures) ? new NextTextureLoadingManager() : null;
  const materialCache = new Map();
  const sceneBox = new THREE.Box3();
  const meshes = adapter.meshes || [];
  const points = adapter.points || [];
  const curves = adapter.curves || [];

  const totalMeshes = meshes.length;
  const totalPoints = points.length;
  const totalCurves = curves.length;
  const totalRenderables = totalMeshes + totalPoints + totalCurves;
  let builtMeshes = 0;
  let builtPoints = 0;
  let builtCurves = 0;
  let lastProgressMesh = 0;
  const progressStep = totalRenderables > 0
    ? Math.max(1, Math.ceil(totalRenderables / Math.max(1, progressInterval)))
    : 1;
  const reportProgress = (message, force = false) => {
    if (!onProgress) return;
    const builtRenderables = builtMeshes + builtPoints + builtCurves;
    if (!force && builtRenderables - lastProgressMesh < progressStep) return;
    lastProgressMesh = builtRenderables;
    onProgress({
      stage: 'building',
      builtMeshes,
      builtPoints,
      builtCurves,
      totalMeshes,
      totalPoints,
      totalCurves,
      materials: materialCache.size,
      queuedTextures: textureManager ? textureManager.total : 0,
      percentage: totalRenderables > 0 ? (builtRenderables / totalRenderables) * 100 : 100,
      message
    });
  };
  const getMaterial = (entry, doubleSided = false) => {
    const key = `${nextMaterialCacheKey(entry)}|side:${doubleSided ? 'double' : 'front'}`;
    let material = materialCache.get(key);
    if (!material) {
      material = createNextMaterial(entry, adapter, textureManager, skipTextures);
      material.side = doubleSided ? THREE.DoubleSide : THREE.FrontSide;
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

  // Reconstruct the USD xform hierarchy from the RenderScene node table.
  // UsdSkel needs it: skeleton binding and mesh placement must share the
  // ancestor transforms (e.g. axis-correcting Z_UP xforms), and
  // findNodeByUSDPath walks nodes by prim name like the legacy tree.
  // Renderables attach at their prim's node — matched by PRIM PATH, never by
  // data id: the adapter indexes renderables by schema order while node
  // dataId refers to RenderScene ids, and the two drift when the converter
  // skips a prim. Anything without a node falls back to flat baked-world
  // placement.
  const nodeGroupByPath = new Map();
  const buildNodeGroup = (node, seen) => {
    if (!node || !node.absPath || seen.has(node.absPath)) return null;
    seen.add(node.absPath);
    const g = new THREE.Group();
    g.name = node.primName || node.displayName || 'node';
    g.userData['primMeta.absPath'] = node.absPath || '';
    if (Array.isArray(node.localMatrix) && node.localMatrix.length === 16) {
      applyUsdRowMajorMatrix(g, node.localMatrix);
    }
    if (node.visible === false) g.visible = false;
    nodeGroupByPath.set(node.absPath, g);
    for (const child of (node.children || [])) {
      const childGroup = buildNodeGroup(child, seen);
      if (childGroup) g.add(childGroup);
    }
    return g;
  };
  {
    const seen = new Set();
    const rootCount = typeof adapter.numRootNodes === 'function' ? adapter.numRootNodes() : 0;
    for (let r = 0; r < rootCount; ++r) {
      const rootGroup = buildNodeGroup(adapter.getRootNode(r), seen);
      if (rootGroup) group.add(rootGroup);
    }
  }
  // Attach a renderable under its prim's node (identity local: the node chain
  // carries the transform). Returns true when attached; false -> caller keeps
  // the flat baked-world placement.
  const attachAtNode = (object3d, primPath) => {
    const parent = primPath ? nodeGroupByPath.get(primPath) : null;
    if (!parent) return false;
    parent.add(object3d);
    return true;
  };

  const pointNodes = new Map();
  const curveNodes = new Map();
  for (const node of adapter.nodes || []) {
    if (node && node.type === 'points' && Number.isFinite(node.dataId) && !pointNodes.has(node.dataId)) {
      pointNodes.set(node.dataId, node);
    }
    if (node && node.type === 'curves' && Number.isFinite(node.dataId) && !curveNodes.has(node.dataId)) {
      curveNodes.set(node.dataId, node);
    }
  }
  const pointSize = (entry) => {
    const widths = entry.widths;
    if (!widths || !widths.length) return 0.02;
    if (widths.length === 1) return Math.max(0.0001, widths[0]);
    let sum = 0;
    let count = 0;
    for (let i = 0; i < widths.length; i++) {
      const value = widths[i];
      if (Number.isFinite(value) && value > 0) {
        sum += value;
        count++;
      }
    }
    return count > 0 ? Math.max(0.0001, sum / count) : 0.02;
  };

  reportProgress(`Building next scene (0/${totalRenderables})...`, true);
  for (const mesh of meshes) {
    builtMeshes++;
    if (!mesh.points || mesh.points.length === 0) {
      reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
      continue;
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(mesh.points, 3));
    if (mesh.normals && mesh.normals.length) {
      geometry.setAttribute('normal', new THREE.BufferAttribute(mesh.normals, 3));
    } else {
      geometry.computeVertexNormals();
    }
    if (mesh.tangents && mesh.tangents.length) {
      geometry.setAttribute('tangent', new THREE.BufferAttribute(mesh.tangents, 4));
    }
    if (mesh.uv0 && mesh.uv0.length) {
      geometry.setAttribute('uv', new THREE.BufferAttribute(mesh.uv0, 2));
      geometry.setAttribute('uv2', new THREE.BufferAttribute(mesh.uv0, 2));
    }
    if (mesh.indices && mesh.indices.length) {
      geometry.setIndex(new THREE.BufferAttribute(mesh.indices, 1));
    }
    if (Array.isArray(mesh.blendShapes) && mesh.blendShapes.length) {
      geometry.morphTargetsRelative = true;
      geometry.morphAttributes.position = [];
      const pointCount = mesh.points.length / 3;
      const addMorph = (offsets, pointIndices, name) => {
        if (!offsets?.length) return;
        let dense = offsets;
        if (pointIndices?.length) {
          dense = new Float32Array(pointCount * 3);
          const sparseCount = Math.min(pointIndices.length, Math.floor(offsets.length / 3));
          for (let i = 0; i < sparseCount; ++i) {
            const point = pointIndices[i];
            if (point >= pointCount) continue;
            dense[point * 3] = offsets[i * 3];
            dense[point * 3 + 1] = offsets[i * 3 + 1];
            dense[point * 3 + 2] = offsets[i * 3 + 2];
          }
        }
        if (dense.length !== mesh.points.length) return;
        const attribute = new THREE.BufferAttribute(dense, 3);
        attribute.name = name;
        geometry.morphAttributes.position.push(attribute);
      };
      for (const shape of mesh.blendShapes) {
        addMorph(shape.pointOffsets, shape.pointIndices, shape.name);
        for (const inbetween of shape.inbetweens || []) {
          addMorph(inbetween.pointOffsets, shape.pointIndices,
            `${shape.name}:${inbetween.name || inbetween.weight}`);
        }
      }
    }
    geometry.computeBoundingBox();
    let material = getMaterial(mesh, !!mesh.doubleSided);
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
      material = materialEntries.map((entry) => getMaterial(entry, !!mesh.doubleSided));
      const shouldCompactGroups = mesh.submeshes.length > Math.max(64, material.length * 8);
      if (shouldCompactGroups) {
        const compacted = compactMaterialGroups(
          mesh.indices, mesh.submeshes, material.length, fallbackIndex,
          mesh.points.length / 3);
        if (compacted) {
          geometry.setIndex(new THREE.BufferAttribute(compacted.indices, 1));
          for (const group of compacted.groups) {
            geometry.addGroup(group.start, group.count, group.materialIndex);
          }
        }
      } else {
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
    }
    const threeMesh = new THREE.Mesh(geometry, material);
    threeMesh.name = mesh.primPath || mesh.primName || `mesh_${mesh.index}`;
    threeMesh.userData['primMeta.absPath'] = mesh.primPath || '';
    threeMesh.userData.usdMesh = {
      index: mesh.index,
      primName: mesh.primName || '',
      primPath: mesh.primPath || '',
      materialId: Number.isFinite(mesh.materialId) ? mesh.materialId : -1,
      materialKey: mesh.materialKey || '',
      doubleSided: !!mesh.doubleSided,
      texturePaths: mesh.texturePaths || {},
      materials: Array.isArray(mesh.materials) ? mesh.materials.map((entry) => ({
        materialId: Number.isFinite(entry.materialId) ? entry.materialId : -1,
        materialKey: entry.materialKey || '',
        material: entry.material || {},
        texturePaths: entry.texturePaths || {}
      })) : [],
      submeshes: Array.isArray(mesh.submeshes) ? mesh.submeshes.map((part) => ({
        start: part.start | 0,
        count: part.count | 0,
        materialIndex: part.materialIndex | 0
      })) : [],
      blendShapes: Array.isArray(mesh.blendShapes) ? mesh.blendShapes.map((shape) => ({
        name: shape.name,
        inbetweens: (shape.inbetweens || []).map((entry) => ({
          name: entry.name,
          weight: entry.weight
        }))
      })) : []
    };
    if (!attachAtNode(threeMesh, mesh.primPath)) {
      applyUsdRowMajorMatrix(threeMesh, mesh.worldMatrix);
      group.add(threeMesh);
    }
    if (geometry.boundingBox && !geometry.boundingBox.isEmpty()) {
      const worldBoxMatrix = new THREE.Matrix4();
      if (Array.isArray(mesh.worldMatrix) && mesh.worldMatrix.length === 16) {
        const m = mesh.worldMatrix;
        worldBoxMatrix.set(
          m[0], m[4], m[8], m[12],
          m[1], m[5], m[9], m[13],
          m[2], m[6], m[10], m[14],
          m[3], m[7], m[11], m[15]
        );
      }
      sceneBox.union(geometry.boundingBox.clone().applyMatrix4(worldBoxMatrix));
    }
    reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
  }

  for (const pointCloud of points) {
    builtPoints++;
    if (!pointCloud.points || pointCloud.points.length === 0) {
      reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
      continue;
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(pointCloud.points, 3));
    const hasColors = pointCloud.colors &&
      pointCloud.colors.length === pointCloud.points.length;
    if (hasColors) {
      geometry.setAttribute('color', new THREE.BufferAttribute(pointCloud.colors, 3));
    }
    geometry.computeBoundingBox();
    const material = new THREE.PointsMaterial({
      size: pointSize(pointCloud),
      sizeAttenuation: true,
      color: hasColors ? new THREE.Color(1, 1, 1) : new THREE.Color(0.78, 0.84, 0.9),
      vertexColors: !!hasColors
    });
    const threePoints = new THREE.Points(geometry, material);
    threePoints.name = pointCloud.primPath || pointCloud.name || `points_${pointCloud.index}`;
    threePoints.userData.usdPoints = {
      index: pointCloud.index,
      name: pointCloud.name || '',
      primPath: pointCloud.primPath || '',
      pointCount: Number.isFinite(pointCloud.pointCount) ? pointCloud.pointCount : 0,
      materialId: Number.isFinite(pointCloud.materialId) ? pointCloud.materialId : -1,
      hasWidths: !!(pointCloud.widths && pointCloud.widths.length),
      hasColors
    };
    const pointNode = pointNodes.get(pointCloud.index);
    if (!attachAtNode(threePoints, pointCloud.primPath)) {
      applyUsdRowMajorMatrix(threePoints, pointNode?.worldMatrix);
      group.add(threePoints);
    }
    if (geometry.boundingBox && !geometry.boundingBox.isEmpty()) {
      const worldBoxMatrix = new THREE.Matrix4();
      const m = pointNode?.worldMatrix;
      if (Array.isArray(m) && m.length === 16) {
        worldBoxMatrix.set(
          m[0], m[4], m[8], m[12],
          m[1], m[5], m[9], m[13],
          m[2], m[6], m[10], m[14],
          m[3], m[7], m[11], m[15]
        );
      }
      sceneBox.union(geometry.boundingBox.clone().applyMatrix4(worldBoxMatrix));
    }
    reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
  }

  for (const curveSet of curves) {
    builtCurves++;
    const tessellated = curveSet.tessellatedPoints;
    const counts = curveSet.tessellatedVertexCounts || [];
    if (!tessellated || tessellated.length === 0 || counts.length === 0) {
      reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
      continue;
    }

    const curveGroup = new THREE.Group();
    curveGroup.name = curveSet.primPath || curveSet.name || `curves_${curveSet.index}`;
    curveGroup.userData['primMeta.absPath'] = curveSet.primPath || '';
    curveGroup.userData.usdCurves = {
      index: curveSet.index,
      name: curveSet.name || '',
      primPath: curveSet.primPath || '',
      curveCount: Number.isFinite(curveSet.curveCount) ? curveSet.curveCount : counts.length,
      type: curveSet.type || 'cubic',
      basis: curveSet.basis || 'bezier',
      wrap: curveSet.wrap || 'nonperiodic',
      isNurbs: !!curveSet.isNurbs,
      materialId: Number.isFinite(curveSet.materialId) ? curveSet.materialId : -1,
      widthsInterpolation: curveSet.widthsInterpolation || 'constant',
      colorsInterpolation: curveSet.colorsInterpolation || 'constant'
    };
    curveGroup.visible = !!showCurves;

    const tessellatedColors = curveSet.tessellatedColors;
    const tessellatedWidths = curveSet.tessellatedWidths;
    let pointOffset = 0;
    for (let curveIndex = 0; curveIndex < counts.length; ++curveIndex) {
      const pointCount = Math.max(0, Number(counts[curveIndex]) | 0);
      if (pointCount < 2 || pointOffset + pointCount > tessellated.length / 3) {
        pointOffset += pointCount;
        continue;
      }
      const positions = tessellated.subarray(pointOffset * 3, (pointOffset + pointCount) * 3);
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
      const hasColors = tessellatedColors &&
        tessellatedColors.length >= (pointOffset + pointCount) * 3;
      if (hasColors) {
        geometry.setAttribute('color', new THREE.BufferAttribute(
          tessellatedColors.subarray(pointOffset * 3, (pointOffset + pointCount) * 3), 3));
      }
      geometry.computeBoundingBox();
      let lineWidth = 1;
      if (tessellatedWidths && tessellatedWidths.length >= pointOffset + pointCount) {
        let widthSum = 0;
        for (let i = pointOffset; i < pointOffset + pointCount; ++i) widthSum += tessellatedWidths[i];
        lineWidth = Math.max(1, widthSum / pointCount);
      } else if (curveSet.widths && curveSet.widths.length === 1) {
        lineWidth = Math.max(1, curveSet.widths[0]);
      }
      const material = new THREE.LineBasicMaterial({
        color: hasColors ? 0xffffff : 0xc7d3e0,
        vertexColors: !!hasColors,
        linewidth: lineWidth
      });
      const line = new THREE.Line(geometry, material);
      line.name = `${curveGroup.name}:${curveIndex}`;
      curveGroup.add(line);
      pointOffset += pointCount;
    }

    const curveNode = curveNodes.get(curveSet.index);
    if (!attachAtNode(curveGroup, curveSet.primPath)) {
      applyUsdRowMajorMatrix(curveGroup, curveNode?.worldMatrix);
      group.add(curveGroup);
    }
    const localBox = new THREE.Box3().setFromObject(curveGroup);
    if (!localBox.isEmpty()) sceneBox.union(localBox);
    reportProgress(`Building next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables})...`);
  }
  reportProgress(`Built next scene (${builtMeshes + builtPoints + builtCurves}/${totalRenderables}), meshes=${builtMeshes}, points=${builtPoints}, curves=${builtCurves}, materials=${materialCache.size}, textures=${textureManager ? textureManager.total : 0}`, true);

  if (!sceneBox.isEmpty()) {
    group.userData.localBoundsBox = sceneBox;
  }

  // Animation channels address nodes by RenderScene node-table index
  // (channel.target_node), NOT by DFS order over the three.js hierarchy —
  // the built tree inserts wrapper groups and attaches renderables as extra
  // children, so a generic buildNodeIndexMap(built.node) DFS would bind
  // tracks to the wrong objects. Map table index -> group by prim path.
  const nodeIndexMap = new Map();
  for (const node of adapter.nodes || []) {
    if (!node || node.error || !Number.isFinite(node.index)) continue;
    const nodeGroup = nodeGroupByPath.get(node.primPath);
    if (nodeGroup) nodeIndexMap.set(node.index, nodeGroup);
  }

  if (releaseBuildData && adapter && typeof adapter.releaseBuildData === 'function') {
    adapter.releaseBuildData();
  }

  return { node: group, textureManager, nodeIndexMap };
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
    nodes: usd && usd.numNodes ? usd.numNodes() : 0,
    animations: usd && usd.numAnimations ? usd.numAnimations() : 0,
    lights: usd && usd.numLights ? usd.numLights() : 0,
    cameras: usd && usd.numCameras ? usd.numCameras() : 0,
    curves: usd && usd.numCurves ? usd.numCurves() : 0,
    pointInstancers: usd && usd.numPointInstancers ? usd.numPointInstancers() : 0,
    pointInstanceDraws: usd && usd.numPointInstanceDraws ? usd.numPointInstanceDraws() : 0,
    skeletons: usd && usd.numSkeletons ? usd.numSkeletons() : 0,
    unsupportedRenderables: usd && usd.numUnsupportedRenderables ? usd.numUnsupportedRenderables() : 0,
    stats
  };
}
