// Regression for TinyUSDZLoaderNative.applyVariantSelection embind overloads.
//   node web/js/tests/apply-variant-selection-overload.test.mjs

import assert from 'node:assert/strict';
import fs from 'node:fs';

import { loadWasm } from '../src/usdzconvert.js';

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

const fixtureUrl = new URL('../../../tests/usda/variantSet-apply-selection-overload.usda', import.meta.url);
const fixtureBytes = new Uint8Array(fs.readFileSync(fixtureUrl));

const wasm64 = process.env.TINYUSDZ_WASM64 === '1';
const glue = wasm64 ? '../src/tinyusdz/tinyusdz_64.js' : '../src/tinyusdz/tinyusdz.js';
const native = await loadWasm(() => import(new URL(glue, import.meta.url).href));

function withLoadedLayer(fn) {
  const usd = new native.TinyUSDZLoaderNative();
  try {
    assert.ok(
      usd.loadAsLayerFromBinary(fixtureBytes, 'variantSet-apply-selection-overload.usda'),
      `loadAsLayerFromBinary failed: ${usd.error()}`
    );
    return fn(usd);
  } finally {
    usd.delete();
  }
}

function assertSelected(usda, selectedName, selectedLevel, rejectedNames) {
  assert.ok(usda.includes(selectedName), `expected selected variant mesh ${selectedName}`);
  assert.ok(usda.includes(`lodLevel = ${selectedLevel}`), `expected lodLevel ${selectedLevel}`);
  for (const name of rejectedNames) {
    assert.ok(!usda.includes(name), `did not expect unselected variant mesh ${name}`);
  }
}

await testAsync('applyVariantSelection(primPath, variantSet, variantName) selects targeted LOD', () => {
  withLoadedLayer((usd) => {
    assert.equal(usd.lodVariantCount(), 3);
    assert.ok(
      usd.applyVariantSelection('/World', 'LOD', 'LOD1'),
      `3-arg applyVariantSelection failed: ${usd.error()}`
    );
    assertSelected(usd.exportAsUSDA(), 'LOD1Mesh', 1, ['LOD0Mesh', 'LOD2Mesh']);
  });
});

await testAsync('applyVariantSelection(variantName) selects authored LOD set globally', () => {
  withLoadedLayer((usd) => {
    assert.equal(usd.lodVariantCount(), 3);
    assert.ok(
      usd.applyVariantSelection('LOD2'),
      `1-arg applyVariantSelection failed: ${usd.error()}`
    );
    assertSelected(usd.exportAsUSDA(), 'LOD2Mesh', 2, ['LOD0Mesh', 'LOD1Mesh']);
  });
});

console.log(`apply-variant-selection-overload tests done (${wasm64 ? 'wasm64' : 'wasm32'})`);
