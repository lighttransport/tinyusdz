import assert from 'node:assert/strict';
import createTinyUSDZ from '../src/tinyusdz/tinyusdz_next.js';

const module = await createTinyUSDZ();
assert.equal(typeof module.LightRTPathTracer, 'function');
const tracer = new module.LightRTPathTracer();
const positions = new Float32Array([-1, -1, 0, 1, -1, 0, 0, 1, 0]);
const normals = new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1]);
const colors = new Float32Array(9).fill(1);
const params = new Float32Array([0, .5, 0, 0, 0, .5, 0, 0, 0, .5, 0, 0]);
const materials = new Float32Array([.8, .2, .1, 0, .5, 0, 0, 0, 0, 0]);
assert.equal(tracer.build(positions, normals, colors, params, new Int32Array([0]), materials), true, tracer.error());
assert.equal(tracer.triangleCount(), 1);
const gpu = tracer.webGPUScene();
assert.equal(gpu.width, 8);
assert.ok(gpu.blocks.length > 0);
const pixels = tracer.trace(new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
  new Float32Array([0, 0, 2]), 16, 16, 0, 1, 2, 1);
assert.equal(pixels.length, 16 * 16 * 4);
let min = Infinity, max = -Infinity;
for (let i = 0; i < pixels.length; i += 4) { min = Math.min(min, pixels[i]); max = Math.max(max, pixels[i]); }
assert.ok(max - min > 0.05, `expected non-uniform result, range=${max - min}`);
tracer.delete();
console.log('LightRT WASM path tracer: PASS');
