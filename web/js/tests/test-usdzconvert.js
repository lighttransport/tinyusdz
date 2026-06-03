// SPDX-License-Identifier: Apache 2.0
// Unit tests for usdzconvert.js pure functions + integration tests.
//
// Run:  vite-node tests/test-usdzconvert.js
//       (from web/js/ directory)

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  isImageName,
  isUsdName,
  imageFormatFromName,
  outputFormatForImage,
  parseByteSize,
  replaceExt,
  rootUsdFromMap,
  loadWasm,
  convertFolderToUSDZ,
} from '../src/usdzconvert.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
let passed = 0;
let failed = 0;

function firstZipEntryName(bytes) {
  assert.equal(bytes[0], 0x50, 'first byte should be P (ZIP magic)');
  assert.equal(bytes[1], 0x4b, 'second byte should be K (ZIP magic)');
  assert.equal(bytes[2], 0x03, 'third byte should be local file header marker');
  assert.equal(bytes[3], 0x04, 'fourth byte should be local file header marker');
  const nameLen = bytes[26] | (bytes[27] << 8);
  const extraLen = bytes[28] | (bytes[29] << 8);
  assert.ok(nameLen > 0, 'first ZIP entry should have a name');
  assert.ok(extraLen >= 0, 'extra field length should be valid');
  const nameBytes = bytes.slice(30, 30 + nameLen);
  return new TextDecoder().decode(nameBytes);
}

function zipEntries(bytes) {
  const entries = new Map();
  let offset = 0;
  const decoder = new TextDecoder();
  while (offset + 30 <= bytes.length) {
    if (bytes[offset] !== 0x50 || bytes[offset + 1] !== 0x4b ||
        bytes[offset + 2] !== 0x03 || bytes[offset + 3] !== 0x04) {
      break;
    }
    const method = bytes[offset + 8] | (bytes[offset + 9] << 8);
    const compressedSize =
      bytes[offset + 18] | (bytes[offset + 19] << 8) |
      (bytes[offset + 20] << 16) | (bytes[offset + 21] << 24);
    const nameLen = bytes[offset + 26] | (bytes[offset + 27] << 8);
    const extraLen = bytes[offset + 28] | (bytes[offset + 29] << 8);
    const nameStart = offset + 30;
    const dataStart = nameStart + nameLen + extraLen;
    const dataEnd = dataStart + compressedSize;
    const name = decoder.decode(bytes.slice(nameStart, nameStart + nameLen));
    assert.equal(method, 0, `ZIP entry ${name} should be stored`);
    assert.ok(dataEnd <= bytes.length, `ZIP entry ${name} should fit`);
    entries.set(name, bytes.slice(dataStart, dataEnd));
    offset = dataEnd;
  }
  return entries;
}

function test(name, fn) {
  try {
    fn();
    passed++;
    console.log(`  ✓ ${name}`);
  } catch (e) {
    failed++;
    console.error(`  ✗ ${name}`);
    console.error(`    ${e.message}`);
  }
}

async function testAsync(name, fn) {
  try {
    await fn();
    passed++;
    console.log(`  ✓ ${name}`);
  } catch (e) {
    failed++;
    console.error(`  ✗ ${name}`);
    console.error(`    ${e.message}`);
  }
}

// ============================================================
console.log('isImageName');
// ============================================================
test('png is image', () => assert.equal(isImageName('tex.png'), true));
test('jpg is image', () => assert.equal(isImageName('tex.jpg'), true));
test('jpeg is image', () => assert.equal(isImageName('tex.jpeg'), true));
test('exr is image', () => assert.equal(isImageName('tex.exr'), true));
test('avif is image', () => assert.equal(isImageName('tex.avif'), true));
test('usda is not image', () => assert.equal(isImageName('scene.usda'), false));
test('txt is not image', () => assert.equal(isImageName('readme.txt'), false));
test('case insensitive PNG', () => assert.equal(isImageName('TEX.PNG'), true));
test('case insensitive JPG', () => assert.equal(isImageName('photo.JPG'), true));
test('path with dirs', () => assert.equal(isImageName('textures/tex.png'), true));
test('no extension', () => assert.equal(isImageName('noext'), false));
test('empty string', () => assert.equal(isImageName(''), false));

// ============================================================
console.log('isUsdName');
// ============================================================
test('usda is usd', () => assert.equal(isUsdName('scene.usda'), true));
test('usdc is usd', () => assert.equal(isUsdName('scene.usdc'), true));
test('usdz is usd', () => assert.equal(isUsdName('scene.usdz'), true));
test('usd is usd', () => assert.equal(isUsdName('scene.usd'), true));
test('png is not usd', () => assert.equal(isUsdName('tex.png'), false));
test('case insensitive USDA', () => assert.equal(isUsdName('SCENE.USDA'), true));
test('path with dirs', () => assert.equal(isUsdName('models/scene.usda'), true));
test('empty string', () => assert.equal(isUsdName(''), false));

// ============================================================
console.log('imageFormatFromName');
// ============================================================
test('png format', () => assert.equal(imageFormatFromName('tex.png'), 'png'));
test('jpg format', () => assert.equal(imageFormatFromName('tex.jpg'), 'jpeg'));
test('jpeg format', () => assert.equal(imageFormatFromName('tex.jpeg'), 'jpeg'));
test('exr returns null', () => assert.equal(imageFormatFromName('tex.exr'), null));
test('avif returns null', () => assert.equal(imageFormatFromName('tex.avif'), null));
test('no extension', () => assert.equal(imageFormatFromName('noext'), null));
test('case insensitive', () => assert.equal(imageFormatFromName('TEX.PNG'), 'png'));

// ============================================================
console.log('outputFormatForImage');
// ============================================================
test('keep png', () => assert.deepEqual(outputFormatForImage('tex.png'), { format: 'png', ext: 'png' }));
test('keep jpeg preserves extension', () => assert.deepEqual(outputFormatForImage('tex.jpeg'), { format: 'jpeg', ext: 'jpeg' }));
test('force jpeg', () => assert.deepEqual(outputFormatForImage('tex.png', 'jpeg'), { format: 'jpeg', ext: 'jpg' }));
test('force png', () => assert.deepEqual(outputFormatForImage('tex.jpg', 'png'), { format: 'png', ext: 'png' }));
test('unsupported keep', () => assert.deepEqual(outputFormatForImage('tex.exr'), { format: null, ext: null }));

// ============================================================
console.log('parseByteSize');
// ============================================================
test('100', () => assert.equal(parseByteSize('100'), 100));
test('100b', () => assert.equal(parseByteSize('100b'), 100));
test('1kb', () => assert.equal(parseByteSize('1kb'), 1024));
test('1KB', () => assert.equal(parseByteSize('1KB'), 1024));
test('1mb', () => assert.equal(parseByteSize('1mb'), 1048576));
test('1MB', () => assert.equal(parseByteSize('1MB'), 1048576));
test('1gb', () => assert.equal(parseByteSize('1gb'), 1073741824));
test('1tb', () => assert.equal(parseByteSize('1tb'), 1099511627776));
test('1.5mb', () => assert.equal(parseByteSize('1.5mb'), Math.floor(1.5 * 1048576)));
test('  100MB  ', () => assert.equal(parseByteSize('  100MB  '), 100 * 1048576));
test('number input', () => assert.equal(parseByteSize(2048), 2048));
test('negative number', () => assert.equal(parseByteSize(-1), 0));
test('invalid string', () => assert.equal(parseByteSize('abc'), 0));
test('empty string', () => assert.equal(parseByteSize(''), 0));
test('null', () => assert.equal(parseByteSize(null), 0));
test('undefined', () => assert.equal(parseByteSize(undefined), 0));
test('float mb', () => assert.equal(parseByteSize('2.5mb'), Math.floor(2.5 * 1048576)));
test('1024', () => assert.equal(parseByteSize('1024'), 1024));

// ============================================================
console.log('replaceExt');
// ============================================================
test('png to jpg', () => assert.equal(replaceExt('a/b.png', 'jpg'), 'a/b.jpg'));
test('no dir', () => assert.equal(replaceExt('tex.png', 'jpg'), 'tex.jpg'));
test('no existing ext', () => assert.equal(replaceExt('tex', 'png'), 'tex.png'));
test('deep path', () => assert.equal(replaceExt('a/b/c/d.exr', 'png'), 'a/b/c/d.png'));
test('multiple dots', () => assert.equal(replaceExt('tex.v2.png', 'jpg'), 'tex.v2.jpg'));

// ============================================================
console.log('rootUsdFromMap');
// ============================================================
test('picks non-usdz over usdz', () => {
  const m = new Map([['a.usdz', null], ['b.usda', null]]);
  assert.equal(rootUsdFromMap(m), 'b.usda');
});
test('picks shallow over deep', () => {
  const m = new Map([['a/b/c.usda', null], ['x.usda', null]]);
  assert.equal(rootUsdFromMap(m), 'x.usda');
});
test('preferred path wins', () => {
  const m = new Map([['deep/scene.usda', null], ['other.usda', null]]);
  assert.equal(rootUsdFromMap(m, 'deep/scene.usda'), 'deep/scene.usda');
});
test('preferred missing falls back', () => {
  const m = new Map([['a.usda', null]]);
  assert.equal(rootUsdFromMap(m, 'missing.usda'), 'a.usda');
});
test('no usd files returns null', () => {
  const m = new Map([['tex.png', null]]);
  assert.equal(rootUsdFromMap(m), null);
});
test('empty map returns null', () => {
  assert.equal(rootUsdFromMap(new Map()), null);
});
test('only usdz files', () => {
  const m = new Map([['a.usdz', null], ['b.usdz', null]]);
  assert.equal(rootUsdFromMap(m), 'a.usdz');
});

// ============================================================
console.log('Integration: loadWasm + convertFolderToUSDZ');
// ============================================================
const wasmJs = path.resolve(__dirname, '../src/tinyusdz/tinyusdz.js');
const hasWasm = fs.existsSync(wasmJs);

if (hasWasm) {
  const wasmGlue = new URL('../src/tinyusdz/tinyusdz.js', import.meta.url).href;

  await testAsync('loadWasm succeeds', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    assert.ok(native, 'native module should be truthy');
    assert.ok(typeof native.TinyUSDZLoaderNative === 'function', 'TinyUSDZLoaderNative should exist');
  });

  // Create a minimal USDA file for conversion testing.
  const tmpDir = path.join(__dirname, '_test_usdzconvert_tmp');
  if (!fs.existsSync(tmpDir)) fs.mkdirSync(tmpDir, { recursive: true });

  const usdaContent = `#usda 1.0
(
    defaultPrim = "root"
    upAxis = "Y"
)

def Xform "root"
{
    def Mesh "cube"
    {
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0,1,2,3, 4,5,6,7, 0,1,5,4, 2,3,7,6, 0,3,7,4, 1,2,6,5]
        point3f[] points = [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (-0.5, 0.5, -0.5)]
    }
}
`;
  const usdaPath = path.join(tmpDir, 'scene.usda');
  fs.writeFileSync(usdaPath, usdaContent);
  const rootWithSublayer = `#usda 1.0
(
    defaultPrim = "root"
    subLayers = [
        @sub.usda@
    ]
)

def Xform "root"
{
}
`;
  const sublayerContent = `#usda 1.0

def Xform "fromSub"
{
}
`;

  await testAsync('convertFolderToUSDZ produces valid USDZ', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('scene.usda', new Uint8Array(fs.readFileSync(usdaPath)));

    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'scene.usda',
    });
    assert.ok(usdz instanceof Uint8Array, 'result should be Uint8Array');
    assert.ok(usdz.length > 0, 'USDZ should not be empty');
    assert.equal(stats.rootPath, 'scene.usda');
    assert.equal(stats.textures, 0, 'no textures in this scene');
    // USDZ magic bytes: "PK" (zip)
    assert.equal(usdz[0], 0x50, 'first byte should be P (ZIP magic)');
    assert.equal(usdz[1], 0x4b, 'second byte should be K (ZIP magic)');
  });

  await testAsync('convertFolderToUSDZ with resize option', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('scene.usda', new Uint8Array(fs.readFileSync(usdaPath)));

    const { usdz } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'scene.usda',
      maxTextureSize: 64,
    });
    assert.ok(usdz.length > 0, 'USDZ with resize should not be empty');
  });

  await testAsync('convertFolderToUSDZ can request USDA root layer', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('scene.usda', new Uint8Array(fs.readFileSync(usdaPath)));

    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'scene.usda',
      rootLayerFormat: 'usda',
    });
    assert.ok(usdz.length > 0, 'USDZ should not be empty');
    assert.equal(stats.rootLayerFormat, 'usda');
    assert.equal(firstZipEntryName(usdz), 'root.usda');
  });

  await testAsync('convertFolderToUSDZ arkitCompatible forces USDC root layer', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('scene.usda', new Uint8Array(fs.readFileSync(usdaPath)));

    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'scene.usda',
      rootLayerFormat: 'usda',
      arkitCompatible: true,
    });
    assert.ok(usdz.length > 0, 'USDZ should not be empty');
    assert.equal(stats.rootLayerFormat, 'usdc');
    assert.equal(firstZipEntryName(usdz), 'root.usdc');
  });

  await testAsync('convertFolderToUSDZ flatten composes local sublayers', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('asset/root.usda', new TextEncoder().encode(rootWithSublayer));
    assetMap.set('asset/sub.usda', new TextEncoder().encode(sublayerContent));

    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'asset/root.usda',
      rootLayerFormat: 'usda',
      flatten: true,
    });
    assert.equal(stats.flatten, true);
    const entries = zipEntries(usdz);
    const root = new TextDecoder().decode(entries.get('root.usda'));
    assert.match(root, /def Xform "fromSub"/);
  });

  await testAsync('convertFolderToUSDZ non-flatten packages local sublayers', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('asset/root.usda', new TextEncoder().encode(rootWithSublayer));
    assetMap.set('asset/sub.usda', new TextEncoder().encode(sublayerContent));

    const { usdz, stats } = await convertFolderToUSDZ(native, assetMap, {
      rootPath: 'asset/root.usda',
      flatten: false,
    });
    assert.equal(stats.flatten, false);
    assert.equal(stats.rootLayerFormat, 'usda');
    const entries = zipEntries(usdz);
    assert.ok(entries.has('root.usda'));
    assert.ok(entries.has('sub.usda'));
    const root = new TextDecoder().decode(entries.get('root.usda'));
    assert.match(root, /@sub\.usda@/);
  });

  await testAsync('convertFolderToUSDZ errors on no USD file', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('readme.txt', new Uint8Array([1, 2, 3]));
    try {
      await convertFolderToUSDZ(native, assetMap);
      assert.fail('should throw');
    } catch (e) {
      assert.ok(e.message.includes('No USD file'), 'error should mention missing USD');
    }
  });

  await testAsync('convertFolderToUSDZ errors on invalid USD bytes', async () => {
    const native = await loadWasm(() => import(wasmGlue));
    const assetMap = new Map();
    assetMap.set('bad.usda', new Uint8Array([0, 1, 2, 3, 4, 5]));
    try {
      await convertFolderToUSDZ(native, assetMap, { rootPath: 'bad.usda' });
      assert.fail('should throw');
    } catch (e) {
      assert.ok(e.message.includes('Failed to load USD'), 'error should mention load failure');
    }
  });

  // Cleanup temp files.
  try { fs.rmSync(tmpDir, { recursive: true }); } catch {}
} else {
  console.log('  (WASM module not found — skipping integration tests)');
}

// ============================================================
// Summary
// ============================================================
console.log(`\n${passed + failed} tests: ${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
