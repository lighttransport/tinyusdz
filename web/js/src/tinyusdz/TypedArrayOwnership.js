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

/**
 * Copy a descriptor-backed array while the native module is still alive.
 * The returned array owns its storage and remains valid after a native scene
 * or RenderStream is ended. The descriptor view itself must never escape this
 * function.
 */
export function copyWasmArray(module, desc, Type, label = 'WASM array') {
  if (!desc || !Number.isFinite(desc.ptr) || !Number.isFinite(desc.length) ||
      desc.length <= 0) {
    return null;
  }
  if (!module?.HEAPU8?.buffer) {
    throw new Error(`${label}: native module heap is unavailable`);
  }
  const view = new Type(module.HEAPU8.buffer, Number(desc.ptr), Number(desc.length));
  const copy = new Type(view);
  if (Type === Float32Array) return markOwnedFloat32Array(copy, label);
  if (Type === Uint32Array) return markOwnedUint32Array(copy, label);
  return copy;
}
