// Smoke test for usdzconvert with the next-core + tydra-next only WASM module.
//
// Runs on the default wasm32 next glue; set TINYUSDZ_WASM64=1 to use
// tinyusdz_next_64.js.

import assert from 'node:assert/strict';

import { convertFolderToUSDZ, loadWasm, unpackUSDZ } from '../src/usdzconvert.js';

const SCENE_USDA = `#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)
def Xform "World"
{
    def Mesh "Tri"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
`;

async function testAsync(name, fn) {
  try {
    await fn();
    console.log(`ok - ${name}`);
  } catch (err) {
    console.error(`not ok - ${name}`);
    console.error(err);
    process.exitCode = 1;
  }
}

const wasm64 = process.env.TINYUSDZ_WASM64 === '1';
const glue = wasm64 ? '../src/tinyusdz/tinyusdz_next_64.js'
                    : '../src/tinyusdz/tinyusdz_next.js';
const glueUrl = new URL(glue, import.meta.url).href;
const wasmDir = new URL('../src/tinyusdz/', import.meta.url);
const native = await loadWasm(() => import(glueUrl), {
  locateFile: (file) => new URL(file, wasmDir).pathname,
});

assert.equal(typeof native.NextUSDZConverterNative, 'function',
  'next-only glue should expose NextUSDZConverterNative');
assert.equal(typeof native.TinyUSDZLoaderNative, 'undefined',
  'next-only glue must not depend on the legacy converter binding');

function assertReloadsWithRenderStream(usdz, label) {
  const stream = new native.RenderStream();
  try {
    const result = stream.begin(usdz);
    assert.ok(result && result.success,
      `${label}: RenderStream should load converted USDZ: ${result?.error || stream.error()}`);
    assert.equal(result.meshCount, 1, `${label}: expected one mesh`);
  } finally {
    stream.end();
    stream.delete();
  }
}

async function convert(rootLayerFormat) {
  const map = new Map([
    ['scene.usda', new TextEncoder().encode(SCENE_USDA)],
    ['textures/dummy.png', new Uint8Array([137, 80, 78, 71])],
  ]);
  return convertFolderToUSDZ(native, map, {
    rootPath: 'scene.usda',
    rootLayerFormat,
    reencode: false,
  });
}

await testAsync('next-only WASM usdzconvert writes USDA root USDZ', async () => {
  const { usdz, stats } = await convert('usda');
  assert.equal(stats.pipeline, 'next-only');
  assert.equal(stats.rootLayerFormat, 'usda');
  const { entries, order } = unpackUSDZ(usdz);
  assert.equal(order[0], 'root.usda');
  assert.ok(new TextDecoder().decode(entries.get('root.usda')).startsWith('#usda'));
  assert.ok(entries.has('textures/dummy.png'), 'pass-through assets should be packaged');
  assertReloadsWithRenderStream(usdz, 'USDA-root USDZ');
});

await testAsync('next-only WASM usdzconvert writes USDC root USDZ', async () => {
  const { usdz, stats } = await convert('usdc');
  assert.equal(stats.pipeline, 'next-only');
  assert.equal(stats.rootLayerFormat, 'usdc');
  const { entries, order } = unpackUSDZ(usdz);
  assert.equal(order[0], 'root.usdc');
  assert.equal(new TextDecoder().decode(entries.get('root.usdc').slice(0, 8)), 'PXR-USDC');
  assertReloadsWithRenderStream(usdz, 'USDC-root USDZ');
});

console.log(`usdzconvert-next-only tests done (${wasm64 ? 'wasm64' : 'wasm32'})`);
