import assert from 'node:assert/strict';
import * as THREE from 'three';

import { LightUSDLoaderUtils } from '../src/lightusd/LightUSDLoaderUtils.js';

const savedLoader = LightUSDLoaderUtils._ktx2Loader;
const savedOwnsLoader = LightUSDLoaderUtils._ownsKTX2Loader;
const calls = [];

function compressedTexture(tag) {
  const texture = new THREE.CompressedTexture(
    [{data: new Uint8Array(16).fill(tag), width: 4, height: 4}],
    4,
    4,
    THREE.RGBA_ASTC_4x4_Format,
    THREE.UnsignedByteType
  );
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  return texture;
}

function scene(image) {
  return {
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
      return image;
    }
  };
}

function ktx2Header(vkFormat, colorModel, supercompression = 0) {
  const data = new Uint8Array(148);
  data.set([
    0xab, 0x4b, 0x54, 0x58, 0x20, 0x32,
    0x30, 0xbb, 0x0d, 0x0a, 0x1a, 0x0a
  ]);
  const view = new DataView(data.buffer);
  view.setUint32(12, vkFormat, true);
  view.setUint32(20, 4, true);
  view.setUint32(24, 4, true);
  view.setUint32(36, 1, true);
  view.setUint32(40, 1, true);
  view.setUint32(44, supercompression, true);
  view.setUint32(48, 104, true);
  view.setUint32(52, 44, true);
  view.setUint32(104, 44, true);
  data[116] = colorModel;
  return data;
}

try {
  LightUSDLoaderUtils.setKTX2Loader({
    loadAsync(uri) {
      calls.push(['load', uri]);
      return Promise.resolve(compressedTexture(3));
    },
    parse(buffer, onLoad) {
      calls.push(['parse', buffer.byteLength]);
      onLoad(compressedTexture(5));
    }
  });

  const external = await LightUSDLoaderUtils.getTextureFromUSD(scene({
    uri: '/textures/material.ktx2?rev=1',
    bufferId: -1,
    decoded: false
  }), 0, 'map');
  assert.equal(external.isCompressedTexture, true);
  assert.equal(external.wrapS, THREE.RepeatWrapping);
  assert.equal(external.wrapT, THREE.ClampToEdgeWrapping);

  const identifier = ktx2Header(158, 162);
  assert.equal(LightUSDLoaderUtils.classifyKTX2Image({data: identifier}).kind,
    'standard');
  const embedded = await LightUSDLoaderUtils.getTextureFromUSD(scene({
    uri: '',
    bufferId: 7,
    decoded: false,
    data: identifier
  }), 1, 'normalMap');
  assert.equal(embedded.isCompressedTexture, true);

  const basis = ktx2Header(0, 166, 2);
  assert.equal(LightUSDLoaderUtils.classifyKTX2Image({data: basis}).kind,
    'basis');
  const embeddedBasis = await LightUSDLoaderUtils.getTextureFromUSD(scene({
    uri: '',
    bufferId: 8,
    decoded: false,
    data: basis
  }), 2, 'map');
  assert.equal(embeddedBasis.isCompressedTexture, true);

  const privateUni = ktx2Header(0, 0, 2);
  assert.equal(LightUSDLoaderUtils.classifyKTX2Image({data: privateUni}).kind,
    'private-uni');
  await assert.rejects(
    LightUSDLoaderUtils.getTextureFromUSD(scene({
      uri: '',
      bufferId: 9,
      decoded: false,
      data: privateUni
    }), 3, 'map'),
    (error) => error?.name === 'UnsupportedTextureFormatError' &&
      error?.ktx2Kind === 'private-uni'
  );
  assert.deepEqual(calls, [
    ['load', '/textures/material.ktx2?rev=1'],
    ['parse', identifier.byteLength],
    ['parse', basis.byteLength]
  ]);

  LightUSDLoaderUtils.setKTX2Loader(null);
  await assert.rejects(
    LightUSDLoaderUtils.getTextureFromUSD(scene({
      uri: 'unavailable.ktx2',
      bufferId: -1,
      decoded: false
    }), 4, 'map'),
    (error) => error?.name === 'UnsupportedTextureFormatError' &&
      error?.extension === 'ktx2'
  );
} finally {
  LightUSDLoaderUtils._ktx2Loader = savedLoader;
  LightUSDLoaderUtils._ownsKTX2Loader = savedOwnsLoader;
}

console.log('loader-utils Basis KTX2: PASS');
