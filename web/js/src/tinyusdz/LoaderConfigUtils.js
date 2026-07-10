import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

export function getBackendFromURL(params = new URLSearchParams(window.location.search), fallback = 'legacy') {
  const backend = params.get('backend');
  return (backend === 'next' || backend === 'auto' || backend === 'legacy')
    ? backend
    : fallback;
}

/**
 * Read a USD asset URI from URL query parameters.
 *
 * Checks the shared aliases (?uri= / ?url= / ?src= / ?model=) plus any
 * page-specific extra aliases (e.g. ['usd']). Returns null when absent.
 */
export function getAssetUriFromURL(params = new URLSearchParams(window.location.search), extraKeys = []) {
  for (const key of ['uri', 'url', 'src', 'model', ...extraKeys]) {
    const value = params.get(key);
    if (value) return value;
  }
  return null;
}

/**
 * Friendly filename derived from a URI/URL path (query/hash stripped,
 * percent-decoded last path segment).
 */
export function basenameFromUri(uri, fallback = 'scene.usd') {
  const clean = String(uri || '').split(/[?#]/)[0];
  return decodeURIComponent(clean.slice(clean.lastIndexOf('/') + 1)) || fallback;
}

export function makeStaticNextParseOptions(options = {}) {
  const backend = options.backend || 'legacy';
  return {
    ...options,
    backend,
    materialDedup: false,
    mergeMeshes: false,
    mergeMeshesBakeTransform: false,
    flattenRenderTree: false
  };
}

/**
 * Apply skinning-related load options to a TinyUSDZLoader instance.
 */
export function applySkinningLoaderOptions(loader, options = {}) {
  if (!loader) return;

  const enableBoneReduction = !!options.enableBoneReduction;
  const targetBoneCount = Number.isFinite(options.targetBoneCount)
    ? options.targetBoneCount
    : 4;
  const roundBoneCount = !!options.roundBoneCount;
  const logger = options.logger || console;

  loader.setEnableBoneReduction(enableBoneReduction);
  loader.setRoundBoneCount(roundBoneCount);

  if (enableBoneReduction) {
    loader.setTargetBoneCount(targetBoneCount);
    logger.log(`Bone reduction enabled: ${targetBoneCount} bones per vertex`);
  }
}

/**
 * Create and initialize a TinyUSDZLoader with common app defaults.
 */
export async function createConfiguredTinyUSDZLoader(options = {}) {
  const initOptions = options.initOptions || {
    useZstdCompressedWasm: false,
    useMemory64: false
  };
  const skinningOptions = options.skinningOptions || {};

  const loader = new TinyUSDZLoader();
  await loader.init(initOptions);
  applySkinningLoaderOptions(loader, skinningOptions);
  return loader;
}

/**
 * Promise wrapper around TinyUSDZLoader.load().
 */
export async function loadUSDSceneFromURL(loader, url, options = {}) {
  return new Promise((resolve, reject) => {
    loader.load(url, resolve, null, reject, options);
  });
}

/**
 * Promise wrapper around TinyUSDZLoader.parse().
 */
export async function parseUSDSceneFromArrayBuffer(loader, arrayBuffer, filename, options = {}) {
  return new Promise((resolve, reject) => {
    loader.parse(new Uint8Array(arrayBuffer), filename, resolve, reject, options);
  });
}
