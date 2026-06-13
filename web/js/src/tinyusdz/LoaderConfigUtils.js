import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

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
export async function loadUSDSceneFromURL(loader, url) {
  return new Promise((resolve, reject) => {
    loader.load(url, resolve, null, reject);
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

