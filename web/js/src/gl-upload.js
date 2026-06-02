// Zero-copy upload of TinyUSDZ mesh heap buffers to WebGL, OpenGL-name style.
//
// getMeshPtr(i) returns per-attribute {ptr, length, comps, dtype} descriptors
// that point into the WASM heap. We build a transient TypedArray on the *live*
// Module.HEAPU8.buffer and hand it straight to gl.bufferData (the only copy is
// the unavoidable CPU->GPU one — no extra JS-heap copy), then keep just the GL
// buffer wrapped in THREE.GLBufferAttribute. The descriptor's `ptr` is a byte
// offset into linear memory and survives heap growth; a TypedArray view does
// NOT, so views are created and consumed synchronously here, never retained.
//
// meshCopyToGeometry() is the fallback for non-triangulated / facevarying
// meshes, built from getMeshCopy(i) (a flat, owned drop-in of getMesh) — safe
// to retain.

import * as THREE from 'three';

// dtype -> { TypedArray, GL type name, bytes/component, normalized }
const DTYPE = {
  f32:     { TA: Float32Array, gl: 'FLOAT',        bytes: 4, normalized: false },
  u32:     { TA: Uint32Array,  gl: 'UNSIGNED_INT', bytes: 4, normalized: false },
  snorm8:  { TA: Int8Array,    gl: 'BYTE',         bytes: 1, normalized: true  },
  snorm16: { TA: Int16Array,   gl: 'SHORT',        bytes: 2, normalized: true  },
};

// Build a transient TypedArray view over the current heap for a descriptor.
// VALID ONLY until the next wasm call/allocation — consume immediately.
export function heapView(module, desc) {
  const d = DTYPE[desc.dtype];
  if (!d) throw new Error(`unsupported dtype: ${desc.dtype}`);
  return new d.TA(module.HEAPU8.buffer, desc.ptr, desc.length);
}

function uploadAttr(gl, module, desc, itemSize) {
  const d = DTYPE[desc.dtype];
  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, heapView(module, desc), gl.STATIC_DRAW); // transient view
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  const attr = new THREE.GLBufferAttribute(buf, gl[d.gl], itemSize, d.bytes, desc.count);
  if ('normalized' in attr) attr.normalized = d.normalized;
  return { attr, buf };
}

function boundsFromHeap(module, pointsDesc) {
  const p = heapView(module, pointsDesc);
  const box = new THREE.Box3();
  const v = new THREE.Vector3();
  for (let i = 0; i < p.length; i += 3) box.expandByPoint(v.set(p[i], p[i + 1], p[i + 2]));
  return box;
}

// Zero-copy geometry for a triangulated, vertex-aligned mesh.
// Returns { geometry, glBuffers } — caller must gl.deleteBuffer(glBuffers) on
// dispose (THREE does not own GLBufferAttribute buffers). Returns null when the
// mesh isn't eligible (non-triangulated or facevarying) — use meshCopyToGeometry.
export function meshPtrToGeometry(gl, module, mptr) {
  if (!mptr || !mptr.points) return null;
  if (!mptr.triangulated) return null;
  const facevarying = (a) => a && a.count !== mptr.vertexCount;
  if (facevarying(mptr.uv0) || facevarying(mptr.normals)) return null;

  const g = new THREE.BufferGeometry();
  const glBuffers = [];
  const add = (name, desc, itemSize) => {
    const { attr, buf } = uploadAttr(gl, module, desc, itemSize);
    g.setAttribute(name, attr);
    glBuffers.push(buf);
  };
  add('position', mptr.points, 3);
  if (mptr.normals) add('normal', mptr.normals, mptr.normals.comps);
  if (mptr.uv0) add('uv', mptr.uv0, 2);

  // THREE requires CPU ownership of the index buffer, so it gets one small
  // owned copy (indices are tiny next to vertex data).
  if (mptr.indices) {
    g.setIndex(new THREE.BufferAttribute(heapView(module, mptr.indices).slice(), 1));
  }

  // GLBufferAttribute carries no CPU positions, so set bounds explicitly.
  g.boundingBox = boundsFromHeap(module, mptr.points);
  g.boundingSphere = g.boundingBox.getBoundingSphere(new THREE.Sphere());
  return { geometry: g, glBuffers };
}

// Fallback geometry from getMeshCopy(i) — a flat, owned drop-in of getMesh()
// (mesh.points, mesh.texcoords, mesh.faceVertexIndices, mesh.normals +
// mesh.normalsFormat). Handles polygon fan-triangulation and facevarying
// de-indexing. Safe to retain.
export function meshCopyToGeometry(mesh) {
  if (!mesh || !mesh.points) return null;
  const g = new THREE.BufferGeometry();
  const points = mesh.points;
  const uv = mesh.texcoords || mesh.uvSets?.uv0?.data || null;
  const normals = (mesh.normalsFormat === 'float32') ? mesh.normals : null;
  const fvi = mesh.faceVertexIndices || null;
  const counts = mesh.faceVertexCounts || null;
  const vertexCount = points.length / 3;
  const facevarying = uv && (uv.length / 2 !== vertexCount);

  // Fan-triangulate.
  const triCorners = [], triVerts = [];
  if (counts && fvi) {
    let o = 0;
    for (let f = 0; f < counts.length; f++) {
      const c = counts[f];
      for (let k = 2; k < c; k++) {
        for (const j of [0, k - 1, k]) { triCorners.push(o + j); triVerts.push(fvi[o + j]); }
      }
      o += c;
    }
  }

  if (facevarying) {
    const n = triCorners.length;
    const pos = new Float32Array(n * 3);
    const uvs = new Float32Array(n * 2);
    const nrm = normals ? new Float32Array(n * 3) : null;
    for (let i = 0; i < n; i++) {
      const vi = triVerts[i], ci = triCorners[i];
      pos[i * 3] = points[vi * 3]; pos[i * 3 + 1] = points[vi * 3 + 1]; pos[i * 3 + 2] = points[vi * 3 + 2];
      uvs[i * 2] = uv[ci * 2]; uvs[i * 2 + 1] = uv[ci * 2 + 1];
      if (nrm) { nrm[i * 3] = normals[ci * 3]; nrm[i * 3 + 1] = normals[ci * 3 + 1]; nrm[i * 3 + 2] = normals[ci * 3 + 2]; }
    }
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
    if (nrm) g.setAttribute('normal', new THREE.BufferAttribute(nrm, 3));
    else g.computeVertexNormals();
  } else {
    g.setAttribute('position', new THREE.BufferAttribute(points, 3));
    if (uv) g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    if (normals) g.setAttribute('normal', new THREE.BufferAttribute(normals, 3));
    if (triVerts.length) g.setIndex(triVerts);
    if (!g.getAttribute('normal')) g.computeVertexNormals();
  }
  return g;
}
