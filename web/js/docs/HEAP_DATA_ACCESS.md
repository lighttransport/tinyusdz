# Heap Data Access: zero-copy `*Ptr` / explicit `*Copy` accessors

How to get mesh and image data out of the TinyUSDZ WASM module and onto the GPU
**without an extra copy across the JS/WASM boundary**, and how to do it safely
given Emscripten's growable heap.

> The GPU upload itself (`gl.bufferData` / `gl.texImage2D`) always copies bytes
> into GPU memory — that is unavoidable. "Zero-copy" here means we skip the
> *intermediate* copy into the JS heap: WebGL reads straight from a view onto
> the WASM heap.

See also [`WASM_TYPED_MEMORY_VIEW.md`](./WASM_TYPED_MEMORY_VIEW.md) for the
separate use-after-`delete()` hazard.

## Mental model (OpenGL-style, id-based)

The loader owns the parsed scene in the WASM heap, addressed by integer id
(`getMeshPtr(i)`, `getImagePtr(i)`, …). Nothing is pushed to JS at load time.
You transfer a resource to the GPU **lazily, when you actually need it**, then
keep only the resulting GL object (buffer/texture) — like an OpenGL name. The
CPU-side bytes stay in the heap, owned by the loader, until you delete or reload
it.

## Why a raw TypedArray view is dangerous (and a pointer is not)

The WASM module is linked with `-sALLOW_MEMORY_GROWTH=1`. Two consequences:

| Event | Effect on a JS `TypedArray` view | Effect on a byte offset (`ptr`) |
|-------|----------------------------------|---------------------------------|
| **Heap grows** (any allocation needing new pages) | The backing `ArrayBuffer` is **detached**; the view becomes zero-length and throws on access | **Unaffected** — linear memory only *appends* pages; existing allocations never move |
| **`loader.delete()` / reload** | Underlying `std::vector` freed → view points at garbage (no error, no detach) | Invalid — do not use |

So a view must be **created from the live `Module.HEAPU8.buffer` and consumed
synchronously**, before any further WASM call. A `ptr` (byte offset) is robust
across heap growth and is the value the accessors hand back.

## API

### Zero-copy: `getMeshPtr(i)` / `getImagePtr(i)`

Return plain descriptors — **no TypedArrays**. Build the view yourself, at the
moment of upload.

`getMeshPtr(i)` →
```js
{
  vertexCount, materialId, doubleSided, primName,
  triangulated,                      // true iff every face is a triangle
  points:           { ptr, length, comps:3, count, dtype:'f32',  byteLength },
  indices:          { ptr, length, comps:1, count, dtype:'u32',  byteLength } | absent,
  faceVertexCounts: { ptr, length, comps:1, count, dtype:'u32',  byteLength } | absent,
  normals:          { ptr, length, comps:3, count, dtype, byteLength } | absent,
  uv0:              { ptr, length, comps:2, count, dtype:'f32',  byteLength } | absent,
}
```

Per-attribute descriptor fields:

| Field | Meaning |
|-------|---------|
| `ptr` | byte offset into `Module.HEAPU8.buffer` (== a WASM C++ pointer) |
| `length` | total scalar count (`count * comps`) — pass this as the view length |
| `comps` | components per vertex (3 for positions/normals, 2 for uv, 1 for index) |
| `count` | vertex/element count (`length / comps`) |
| `dtype` | `'f32'`, `'u32'`, `'snorm8'`, or `'snorm16'` |
| `byteLength` | `length * sizeof(dtype)` |

`getImagePtr(i)` →
```js
{ width, height, channels, decoded, colorSpace, usdColorSpace, uri,
  ptr, byteLength }              // ptr/byteLength absent if the image has no buffer
```

Normal formats: TinyUSDZ may store normals packed. `getMeshPtr` exposes
`snorm8` / `snorm16` directly (upload with `normalized = true`); packed
`1010102` normals are unpacked once into a stable cache and exposed as `f32`.

### Owned copy: `getMeshCopy(i)` / `getImageCopy(i)`

A **drop-in replacement** for `getMesh(i)` / `getImage(i)`: the **exact same
object shape**, but every heap-backed TypedArray (including nested ones like
`uvSets.uvN.data`) is an **owned JS-heap copy** — safe to retain, hand to
`THREE.BufferAttribute` / `DataTexture`, or process on the CPU. Use these when a
view/ptr doesn't apply, e.g. assembling a UDIM atlas / `DataArrayTexture` on a
canvas (resize, flip, channel-pack) before upload, or any CPU mesh work.

```js
// identical to getMesh()/getImage(), just owned arrays:
mesh:  { points: Float32Array, normals, normalsFormat, texcoords, uvSets,
         faceVertexIndices, faceVertexCounts, tangents, materialId, ... }
image: { width, height, channels, decoded, colorSpace, bufferId, data: Uint8Array }
```

Migrating existing `getMesh`/`getImage` callers is therefore a mechanical rename
(`getMesh` → `getMeshCopy`, `getImage` → `getImageCopy`) with no call-site
changes.

### Deprecated: `getMesh(i)` / `getImage(i)`

Kept for backward compatibility; each logs a one-time `console.warn`. They
return heap-aliasing views with **no explicit lifetime contract** (the original
source of corruption bugs). Migrate to `*Copy` (owned, drop-in) or, for GPU
upload, `*Ptr` (zero-copy).

## Lifecycle contract

1. Call `getMeshPtr(i)` / `getImagePtr(i)` **immediately before** uploading.
2. Build the view from the **current** buffer: `new Float32Array(module.HEAPU8.buffer, desc.ptr, desc.length)`.
3. Upload it (`gl.bufferData` / `gl.texImage2D`) **before any other WASM call**.
4. Keep only the GL object. Never store the view.
5. `ptr`s are invalid after `loader.delete()` or a re-load.

## Consumer patterns

### Raw WebGL

```js
const m = usd.getMeshPtr(i);
const buf = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, buf);
gl.bufferData(gl.ARRAY_BUFFER,
  new Float32Array(module.HEAPU8.buffer, m.points.ptr, m.points.length), // transient
  gl.STATIC_DRAW);
// keep `buf`; the view is now discardable
```

```js
const im = usd.getImagePtr(imageId);
const view = new Uint8Array(module.HEAPU8.buffer, im.ptr, im.byteLength);   // transient
const fmt = im.channels === 4 ? gl.RGBA : gl.RGB;
gl.texImage2D(gl.TEXTURE_2D, 0, fmt, im.width, im.height, 0, fmt, gl.UNSIGNED_BYTE, view);
```

### Three.js (helper: `web/js/src/gl-upload.js`)

`meshPtrToGeometry(gl, module, mptr)` uploads each vertex attribute to a GL
buffer and wraps it in `THREE.GLBufferAttribute` (so Three never holds a CPU
view). It returns `{ geometry, glBuffers }`.

```js
import { meshPtrToGeometry, meshCopyToGeometry } from './src/gl-upload.js';

const gl = renderer.getContext();
const mptr = usd.getMeshPtr(i);
const built = meshPtrToGeometry(gl, native, mptr);     // null if not eligible
const geometry = built ? built.geometry
                       : meshCopyToGeometry(usd.getMeshCopy(i));   // fallback
const mesh = new THREE.Mesh(geometry, material);
if (built) mesh.userData.glBuffers = built.glBuffers;  // delete on dispose
```

Caveats baked into the helper:
- **Index is copied once.** Three.js requires CPU ownership of the index
  buffer, so the index gets a single small owned copy (`slice()`); vertex data
  stays zero-copy. (Indices are tiny next to vertex data.)
- **Bounds are set manually.** `GLBufferAttribute` carries no CPU positions, so
  `boundingBox`/`boundingSphere` are computed from a transient points view.
- **You own the GL buffers.** Three does not delete `GLBufferAttribute` buffers;
  call `gl.deleteBuffer()` on each (`mesh.userData.glBuffers`) when disposing.
- **Eligibility.** `meshPtrToGeometry` returns `null` for non-triangulated or
  facevarying meshes (the index can't address per-corner attributes); fall back
  to `meshCopyToGeometry(getMeshCopy(i))`, which fan-triangulates / de-indexes
  on owned arrays.

### dtype → WebGL

| `dtype` | TypedArray | `gl` type | `normalized` | bytes/comp |
|---------|-----------|-----------|--------------|------------|
| `f32` | `Float32Array` | `gl.FLOAT` | false | 4 |
| `u32` | `Uint32Array` | `gl.UNSIGNED_INT` | false | 4 |
| `snorm8` | `Int8Array` | `gl.BYTE` | true | 1 |
| `snorm16` | `Int16Array` | `gl.SHORT` | true | 2 |

## Choosing an accessor

| Use | Accessor |
|-----|----------|
| Upload geometry straight to the GPU for rendering | `getMeshPtr` |
| Upload an image straight to a GL texture | `getImagePtr` |
| CPU work on pixels (UDIM atlas/array-texture, resize, flip, channel pack) | `getImageCopy` |
| Retain mesh arrays on the JS side / non-triangulated fallback | `getMeshCopy` |
| Existing code (will warn) | `getMesh` / `getImage` — migrate away |

## Worked example

`web/js/udim.js` uses both paths: geometry via `getMeshPtr` +
`meshPtrToGeometry` (zero-copy), and UDIM tile pixels via `getImageCopy`
(CPU-side packing into a `THREE.DataArrayTexture`). It calls neither deprecated
accessor, so no deprecation warnings fire.
