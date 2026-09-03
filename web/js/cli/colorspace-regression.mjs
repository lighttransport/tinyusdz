#!/usr/bin/env node
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import process from 'node:process';
import {
  MACBETH_COLORCHECKER_AP0,
  linearColorTransformMatrix,
  transformLinearColor
} from '../src/lightusd/ColorCalibrationTestKit.js';

const matrix = linearColorTransformMatrix('lin_ap0_scene', 'lin_rec709_scene');
assert.equal(matrix.length, 9);
assert.equal(MACBETH_COLORCHECKER_AP0.length, 24);
for (const patch of MACBETH_COLORCHECKER_AP0) {
  const converted = transformLinearColor(patch);
  assert.equal(converted.length, 3);
  assert(converted.every(Number.isFinite));
}
const neutral = transformLinearColor(MACBETH_COLORCHECKER_AP0[18]);
assert(Math.max(...neutral) - Math.min(...neutral) < 0.025,
  'white patch must remain approximately neutral');

const numeric = { pass: true, patches: 24, source: 'lin_ap0_scene',
  destination: 'lin_rec709_scene', matrix };
console.log(JSON.stringify(numeric, null, 2));

if (process.argv.includes('--browser')) {
  const run = spawnSync(process.execPath,
    [new URL('../tests/colorspace-render-batch.mjs', import.meta.url).pathname],
    { stdio: 'inherit', env: process.env });
  if (run.status !== 0) process.exitCode = run.status || 1;
}

if (process.argv.includes('--wasm')) {
  const run = spawnSync(process.execPath,
    [new URL('../tests/colorspace-backend-parity.mjs', import.meta.url).pathname],
    { stdio: 'inherit', env: process.env });
  if (run.status !== 0) process.exitCode = run.status || 1;
}
