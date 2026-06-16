// Smoke test for the WASM `next` (low-memory lazy-ValueRep) flatten pipeline.
//   node tests/usdzconvert-next.test.mjs
//
// The `next` pipeline (src/next/) is compiled into the WASM module and exposed
// through convertFolderToUSDZ({ pipeline: 'next' }) for a single .usdz with a
// top-level USDC root. This test exercises that path end-to-end and asserts it
// actually engaged (not the legacy fallback) and produced a reloadable .usdz.
//
// Runs on the default wasm32 glue; set TINYUSDZ_WASM64=1 to use the 64-bit glue.

import assert from 'node:assert/strict';

import { loadWasm, convertFolderToUSDZ } from '../src/usdzconvert.js';

async function testAsync(name, fn) {
  try { await fn(); console.log(`ok - ${name}`); }
  catch (err) { console.error(`not ok - ${name}`); console.error(err); process.exitCode = 1; }
}

// Tiny self-contained scene (no external textures), used to build the input.
const SCENE_USDA = `#usda 1.0
(
    defaultPrim = "World"
)
def Xform "World"
{
    def Mesh "Box"
    {
        int[] faceVertexCounts = [3]
        int[] faceVertexIndices = [0, 1, 2]
        point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    }
}
`;

const wasm64 = process.env.TINYUSDZ_WASM64 === '1';
const glue = wasm64 ? '../src/tinyusdz/tinyusdz_64.js' : '../src/tinyusdz/tinyusdz.js';
const glueUrl = new URL(glue, import.meta.url).href;
const native = await loadWasm(() => import(glueUrl));

// Build a single .usdz with a top-level USDC root via the legacy path. This is
// the shape the `next` pipeline requires as input.
function makeUsdzWithUsdcRoot() {
  const map = new Map([['scene.usda', new TextEncoder().encode(SCENE_USDA)]]);
  return convertFolderToUSDZ(native, map, {
    rootPath: 'scene.usda',
    rootLayerFormat: 'usdc',   // top-level USDC root, required by `next`
    reencode: false,
  });
}

// Confirm an output is a real (zip) USDZ and reloads in the WASM loader.
function assertReloadableUsdz(usdz, label) {
  assert.ok(usdz instanceof Uint8Array && usdz.length > 64, `${label}: usdz bytes present`);
  // ZIP local-file-header magic: "PK\x03\x04".
  assert.deepEqual(Array.from(usdz.slice(0, 4)), [0x50, 0x4b, 0x03, 0x04],
    `${label}: USDZ should start with the ZIP magic`);
  const usd = new native.TinyUSDZLoaderNative();
  try {
    assert.ok(usd.loadFromBinary(usdz, 'out.usdz'),
      `${label}: produced USDZ should reload: ${usd.error()}`);
  } finally { usd.delete(); }
}

await testAsync('next pipeline: flattens a single USDC-root .usdz and reloads', async () => {
  const base = await makeUsdzWithUsdcRoot();
  assertReloadableUsdz(base.usdz, 'base (legacy)');

  const log = [];
  const map = new Map([['scene.usdz', base.usdz]]);
  const { usdz, stats } = await convertFolderToUSDZ(native, map, {
    rootPath: 'scene.usdz',
    pipeline: 'next',
    reencode: false,
    log: (m) => log.push(String(m)),
  });

  // The `next` path must have engaged (not the legacy fallback).
  const declined = log.some((l) => /next pipeline (declined|error)/.test(l));
  assert.ok(!declined, `next pipeline must not decline/fall back. log:\n${log.join('\n')}`);
  assert.equal(stats.pipeline, 'next', `stats.pipeline should be 'next' (got '${stats.pipeline}')`);

  // And it must produce a valid, reloadable USDZ.
  assertReloadableUsdz(usdz, 'next');
});

console.log(`usdzconvert-next tests done (${wasm64 ? 'wasm64' : 'wasm32'})`);
