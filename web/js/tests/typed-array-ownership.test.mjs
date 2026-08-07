import assert from 'node:assert/strict';

import {
  copyWasmArray,
  isOwnedFloat32Array,
  isOwnedUint32Array,
  toOwnedFloat32Array,
  toOwnedUint32Array,
} from '../src/tinyusdz/TypedArrayOwnership.js';

const heap = new ArrayBuffer(64);
new Float32Array(heap, 8, 3).set([1, 2, 3]);
new Uint32Array(heap, 24, 2).set([7, 9]);
const module = { HEAPU8: new Uint8Array(heap) };

const points = copyWasmArray(module, { ptr: 8, length: 3 }, Float32Array, 'points');
const indices = copyWasmArray(module, { ptr: 24, length: 2 }, Uint32Array, 'indices');
assert.deepEqual(Array.from(points), [1, 2, 3]);
assert.deepEqual(Array.from(indices), [7, 9]);
assert.equal(isOwnedFloat32Array(points), true);
assert.equal(isOwnedUint32Array(indices), true);

new Float32Array(heap, 8, 3).fill(0);
new Uint32Array(heap, 24, 2).fill(0);
assert.deepEqual(Array.from(points), [1, 2, 3], 'copied WASM data must survive heap reuse');
assert.deepEqual(Array.from(indices), [7, 9], 'copied indices must survive heap reuse');

const input = new Float32Array([4, 5]);
const ownedInput = toOwnedFloat32Array(input);
assert.notEqual(ownedInput, input, 'unmarked arrays should be copied into owned storage');
assert.equal(toOwnedFloat32Array(ownedInput), ownedInput, 'owned arrays should not be copied twice');
assert.deepEqual(Array.from(toOwnedUint32Array([2, 4])), [2, 4]);

console.log('typed array ownership: PASS');
