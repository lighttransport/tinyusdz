import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { LightUSDLoader } from '../src/lightusd/LightUSDLoader.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '../../..');
const inputFile = path.join(repoRoot, 'models/polysphere-materialx-001.usda');
const fixtureFile = path.join(here, 'fixtures/materialx-polysphere-nlohmann.json');

function readArrayBuffer(filename) {
  const data = fs.readFileSync(filename);
  return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
}

async function loadUSD(filename) {
  // This fixture locks the legacy native MaterialX JSON schema, which is
  // intentionally different from the next RenderStream material payload.
  const loader = new LightUSDLoader(null, { backend: 'legacy' });
  await loader.init({ useMemory64: false, backend: 'legacy' });
  loader.setMaxMemoryLimitMB(500);

  return new Promise((resolve, reject) => {
    loader.parse(readArrayBuffer(filename), filename, resolve, reject);
  });
}

function materialToJSON(usd, materialId) {
  const result = usd.getMaterialWithFormat(materialId, 'json');
  assert.equal(result.error == null, true, `material ${materialId} JSON export failed`);
  return JSON.parse(result.data);
}

const usd = await loadUSD(inputFile);
const actual = Array.from({ length: usd.numMaterials() }, (_, id) => materialToJSON(usd, id));
const expected = JSON.parse(fs.readFileSync(fixtureFile, 'utf8'));

assert.deepEqual(actual, expected);
assert.equal(actual.length, 1);
assert.equal(actual[0].name, 'Material_001');
assert.equal(actual[0].hasOpenPBR, true);
assert.equal(actual[0].hasUsdPreviewSurface, true);
assert.equal(actual[0].openPBR?.nodeGraph?.nodegraph?.nodes?.length, 6);
assert.equal(actual[0].openPBR?.nodeGraph?.nodegraph?.outputs?.length, 1);

console.log(`MaterialX JSON regression passed: ${actual.length} material(s) matched nlohmann fixture`);
