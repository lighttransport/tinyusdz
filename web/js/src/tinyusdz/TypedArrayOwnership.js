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

const ownedFloat32Arrays = new WeakSet();
const ownedUint32Arrays = new WeakSet();

export function markOwnedFloat32Array(data, label = 'data') {
  if (!(data instanceof Float32Array)) {
    throw new Error(`${label} must be a Float32Array`);
  }
  ownedFloat32Arrays.add(data);
  return data;
}

export function markOwnedUint32Array(data, label = 'data') {
  if (!(data instanceof Uint32Array)) {
    throw new Error(`${label} must be a Uint32Array`);
  }
  ownedUint32Arrays.add(data);
  return data;
}

export function isOwnedFloat32Array(data) {
  return data instanceof Float32Array && ownedFloat32Arrays.has(data);
}

export function isOwnedUint32Array(data) {
  return data instanceof Uint32Array && ownedUint32Arrays.has(data);
}

export function toOwnedFloat32Array(data, label = 'data') {
  assertArrayLike(data, label);
  if (isOwnedFloat32Array(data)) return data;
  return markOwnedFloat32Array(new Float32Array(data), label);
}

export function toOwnedUint32Array(data, label = 'data') {
  assertArrayLike(data, label);
  if (isOwnedUint32Array(data)) return data;
  return markOwnedUint32Array(new Uint32Array(data), label);
}
