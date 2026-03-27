/**
 * Typed array ownership helpers.
 *
 * Copies array-like sources into JS-owned typed arrays so callers do not retain
 * references to transient WASM typed_memory_view buffers.
 */

function assertArrayLike(data, label) {
  if (data == null || typeof data.length !== 'number') {
    throw new Error(`${label} must be an array-like object`);
  }
}

export function toOwnedFloat32Array(data, label = 'data') {
  assertArrayLike(data, label);
  return new Float32Array(data);
}

export function toOwnedUint32Array(data, label = 'data') {
  assertArrayLike(data, label);
  return new Uint32Array(data);
}

