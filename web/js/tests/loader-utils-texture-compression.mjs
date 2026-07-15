import assert from 'node:assert/strict';

import { TinyUSDZLoaderUtils } from '../src/tinyusdz/TinyUSDZLoaderUtils.js';

const savedModule = TinyUSDZLoaderUtils.getTinyUSDZ();
const savedDetector = TinyUSDZLoaderUtils.detectTextureCompressionTarget;
const savedEnabled = TinyUSDZLoaderUtils.sceneTextureCompressionEnabled;
const calls = [];

try {
  TinyUSDZLoaderUtils.detectTextureCompressionTarget = () => ({
    target: 'bc7',
    linearFormat: 0x8e8c,
    srgbFormat: 0x8e8d,
    name: 'BC7'
  });
  TinyUSDZLoaderUtils.setSceneTextureCompressionEnabled(true);
  TinyUSDZLoaderUtils.setTinyUSDZ({
    compressTextureToUni(data, width, height, flipY) {
      calls.push(['compress', data.byteLength, width, height, flipY]);
      return { success: true, data: new Uint8Array(16).fill(7), byteLength: 16 };
    },
    transcodeTextureUni(data, width, height, target) {
      calls.push(['transcode', data.byteLength, width, height, target]);
      return { success: true, data: new Uint8Array(16).fill(9), byteLength: 16 };
    }
  });

  const rgba = new Uint8Array(4 * 4 * 4).fill(255);
  const usdScene = {
    getTexture() {
      return {
        textureImageId: 0,
        wrapS: 'repeat',
        wrapT: 'clamp_to_edge',
        hasTransform2d: false,
        isUDIM: false
      };
    },
    getImageCopy() {
      return {
        uri: 'decoded-scene.ktx2',
        bufferId: 0,
        decoded: true,
        width: 4,
        height: 4,
        channels: 4,
        data: rgba
      };
    }
  };

  const compressed = await TinyUSDZLoaderUtils.getTextureFromUSD(usdScene, 0);
  assert.equal(compressed.isCompressedTexture, true);
  assert.equal(compressed.format, 0x8e8c);
  assert.equal(compressed.colorSpace, '');
  assert.equal(compressed.flipY, false);
  assert.equal(compressed.mipmaps[0].data.byteLength, 16);
  assert.deepEqual(calls, [
    ['compress', 64, 4, 4, true],
    ['transcode', 16, 4, 4, 'bc7']
  ]);
  assert.deepEqual(compressed.userData.tinyusdzCompression, {
    format: 'BC7',
    rgbaBytes: 64,
    uniBytes: 16,
    gpuBytes: 16,
    linearFormat: 0x8e8c,
    srgbFormat: 0x8e8d
  });

  TinyUSDZLoaderUtils.applyTextureMapDefaults(compressed, 'map');
  assert.equal(compressed.format, 0x8e8c);
  assert.equal(compressed.colorSpace, 'srgb');

  TinyUSDZLoaderUtils.setSceneTextureCompressionEnabled(false);
  const fallback = await TinyUSDZLoaderUtils.getTextureFromUSD(usdScene, 0);
  assert.equal(fallback.isDataTexture, true);
  assert.equal(fallback.flipY, true);
  assert.equal(calls.length, 2);
} finally {
  TinyUSDZLoaderUtils.setTinyUSDZ(savedModule);
  TinyUSDZLoaderUtils.detectTextureCompressionTarget = savedDetector;
  TinyUSDZLoaderUtils.setSceneTextureCompressionEnabled(savedEnabled);
}

console.log('loader-utils texture compression: PASS');
