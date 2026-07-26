import assert from 'node:assert/strict';

import {
  MAX_TEXTURE_CONCURRENCY,
  normalizeTextureConcurrency,
  textureConcurrencyForBudget,
} from '../src/texture-memory-budget.mjs';

const MIB = 1024 * 1024;

assert.equal(normalizeTextureConcurrency(8), 8);
assert.equal(normalizeTextureConcurrency(2.9), 2);
assert.equal(normalizeTextureConcurrency(0, 4), 4);
assert.equal(normalizeTextureConcurrency(0, 0.5), 1);
assert.equal(normalizeTextureConcurrency(-1), 1);
assert.equal(normalizeTextureConcurrency(Number.NaN), 1);
assert.equal(normalizeTextureConcurrency(Number.POSITIVE_INFINITY), 1);
assert.equal(normalizeTextureConcurrency(1e9), MAX_TEXTURE_CONCURRENCY);

assert.equal(textureConcurrencyForBudget(8, 0), 8);
assert.equal(textureConcurrencyForBudget(8, 384 * MIB), 1);
assert.equal(textureConcurrencyForBudget(8, 576 * MIB), 1);
assert.equal(textureConcurrencyForBudget(8, 768 * MIB), 2);
assert.equal(textureConcurrencyForBudget(8, 1024 * MIB), 3);
assert.equal(textureConcurrencyForBudget(1e9, Number.POSITIVE_INFINITY),
  MAX_TEXTURE_CONCURRENCY);

console.log('texture memory budget: PASS');
