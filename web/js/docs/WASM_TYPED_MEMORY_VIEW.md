# WASM typed_memory_view and Data Copying

> **Prefer the explicit-lifetime accessors.** New code should use the id-based
> `getMeshPtr`/`getImagePtr` (zero-copy) and `getMeshCopy`/`getImageCopy`
> (owned) accessors documented in [`HEAP_DATA_ACCESS.md`](./HEAP_DATA_ACCESS.md).
> `getMesh()`/`getImage()` (discussed below) are deprecated and warn at runtime;
> their returned views alias the heap and have no lifetime contract — the source
> of the corruption described here. This document remains as the post-mortem and
> applies to any remaining `getMesh()`/`getImage()` users.

## Summary

All data retrieved from Emscripten `typed_memory_view` must be copied into JS-owned buffers
before `usd_scene.delete()` is called. The C++ destructor frees the underlying memory, turning
valid-looking JS TypedArrays into views of freed (garbage) data.

## The Problem

CesiumMan's chest had a visible vertex dip. Investigation traced it to vertex 0 having its X,Y
coordinates corrupted from `(0.0934, 0.0487)` to `(5.36e-40, 5.36e-40)` (hex `0x0005d760` both).

## Root Cause

`processUSDScene()` ends with:

```javascript
// Release WASM scene object
if (usd_scene && typeof usd_scene.delete === 'function') {
    usd_scene.delete();
}
```

This calls the C++ destructor on `TinyUSDZLoaderNative`, which frees all `render_scene_` vectors
(`points`, `normals`, `texcoords`, `jointIndices`, `jointWeights`, `sampler.times/values`, etc.).
The WASM allocator reclaims that memory and overwrites it with internal bookkeeping data.

### Why It's Not Heap Growth

Heap monitoring confirmed the WASM heap **never grew** during CesiumMan loading (constant at
17,694,720 bytes). The ArrayBuffer is never detached or replaced. The corruption mechanism is:

1. `getMesh()` returns `typed_memory_view` pointing to `render_scene_.meshes[i].points.data()`
2. `BufferAttribute` stores a reference to this TypedArray (no copy)
3. `usd_scene.delete()` runs the C++ destructor, freeing the `std::vector` memory
4. The WASM allocator reuses the freed region for bookkeeping
5. The TypedArray still points to the same offset in the same ArrayBuffer, but the data is garbage

### Why It's Hard to Detect

- The JS ArrayBuffer is **not detached** (`.byteLength` stays the same)
- The TypedArray is still a valid JS object (no errors on access)
- The data was correct at every checkpoint during `processUSDScene()`
- Corruption only appears after the function returns (where `delete()` is called)
- Only some bytes are overwritten (Z coordinate of vertex 0 was untouched)

## The Fix

Copy all WASM-backed arrays into JS-owned buffers immediately when retrieved:

### Mesh Geometry (`TinyUSDZLoaderUtils.js: convertUsdMeshToThreeMesh`)

```javascript
geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(mesh.points), 3));
geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(mesh.faceVertexIndices), 1));
// Same for: texcoords, normals, vertexColors, tangents
```

### Skinning Data (`skin-anim.js: processUSDScene`)

```javascript
const meshData = {
    jointIndices: new Int32Array(mesh.jointIndices),
    jointWeights: new Float32Array(mesh.jointWeights),
    // ...
};
```

### Animation Data (`USDAnimationConverter.js`)

```javascript
new THREE.VectorKeyframeTrack(name, new Float32Array(sampler.times), new Float32Array(sampler.values), ...);
new THREE.QuaternionKeyframeTrack(name, new Float32Array(sampler.times), new Float32Array(sampler.values), ...);
```

## What Does NOT Need Copying

- **`USDSkeletalHelper.js`**: Skeleton data (`bindTransforms`, `restTransforms`) is consumed
  immediately via `parseMatrix()`/`decompose()` during bone construction. The results are stored
  in Three.js `Bone` objects, not as WASM views.

- **Scalar values and strings**: `mesh.materialId`, `mesh.absPath`, `mesh.doubleSided`, etc.
  are copied by value when passed through Emscripten's embind.

## Alternative: Skip delete()

If `usd_scene.delete()` were removed, no copying would be needed since the C++ object (and its
`render_scene_` data) would remain alive in WASM heap memory. However, this wastes memory -
the entire parsed USD scene stays resident. For CesiumMan this is ~17MB; for larger models
(AnimFinal_LowRes with 3001 joints) it can be significantly more. The one-time copy cost at
load time is negligible compared to keeping the full scene in memory.
