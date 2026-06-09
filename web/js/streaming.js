// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-present Light Transport Entertainment, Inc.
//
// streaming.js — low-memory, streaming USD/USDZ renderer for the browser.
//
// Goal: render a USD/USDZ scene (geometry + textures + basic UsdPreviewSurface /
// MaterialX PBR shading) while keeping the WASM linear heap close to the INPUT
// FILE SIZE — not the 5–10x blow-up of a full typed-stage import.
//
// How it stays low-memory:
//   1. Input is streamed into the WASM heap once (chunked HEAPU8.set into a
//      zero-copy buffer), so the input exists in the heap exactly once.
//   2. Textures are NOT decoded in WASM (loadTextureInNative=false): images stay
//      as their original encoded bytes (PNG/JPEG/...) in the heap — roughly the
//      same size they occupy inside the .usdz — and are decoded per-texture in JS
//      via createImageBitmap (off the WASM heap, GPU-accelerated).
//   3. Geometry is read through the binding's ZERO-COPY descriptors
//      (getMeshPtr -> {ptr,length,dtype}) as typed-array views into the heap and
//      uploaded straight to GL buffers. Nothing is copied into JS first.
//   4. As soon as every mesh and texture is on the GPU, usd.reset() frees the
//      entire WASM-side render scene (geometry + encoded images + input). The GL
//      buffers/textures live in GPU memory, independent of the WASM heap, so the
//      resident WASM working set drops to ~0 while rendering.
//
// Memory reality / known limitation: the resident footprint is bounded by the
// above, but the *transient conversion peak* is NOT yet at the "≈ input" target.
// loadFromBinary() converts the WHOLE scene eagerly (parse a typed Stage, then
// build the RenderScene), so during the load it holds input + Stage + render
// data at once — measured at ~5–10x input on large scenes — and emscripten never
// returns grown heap to the OS, so process RSS stays at that peak. Hitting the
// "WASM heap ≈ input" target requires an INCREMENTAL render-data converter
// (decode→emit→free one mesh/image at a time over the next-pipeline lazy crate),
// which is a C++ binding addition tracked as the next step. The memory panel
// reports the honest per-phase heap so the peak is visible, not hidden.
//
// The renderer is self-contained raw WebGL2 (no Three.js): a compact GGX + Smith
// + Schlick direct light plus a hemispheric ambient term, with base-color /
// normal / metallic-roughness / occlusion / emissive texture support. Material
// parameters come from getMaterial(matId), which tydra resolves identically for
// UsdPreviewSurface AND MaterialX networks — so "basic MaterialX shading" needs
// no JS-side network evaluation here.
//
// Usage (module):
//   import { StreamingUSDRenderer, mountStreamingDemo } from './streaming.js';
//   const r = new StreamingUSDRenderer(canvas);
//   await r.init();
//   const stats = await r.loadBytes(new Uint8Array(buf), 'scene.usdz');
//   // stats.memory has the per-phase heap readout; r.onMemory(cb) for live polls.
//
// or just include streaming.html, which calls mountStreamingDemo().

import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';

// ---------------------------------------------------------------------------
// Small math helpers (column-major mat4, like WebGL/GLSL).
// ---------------------------------------------------------------------------

const M = {
  ident: () => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
  mul(a, b) {
    const o = new Float32Array(16);
    for (let c = 0; c < 4; c++) {
      for (let r = 0; r < 4; r++) {
        o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                       a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
      }
    }
    return o;
  },
  perspective(fovy, aspect, near, far) {
    const f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
    const o = new Float32Array(16);
    o[0] = f / aspect; o[5] = f; o[10] = (far + near) * nf; o[11] = -1;
    o[14] = 2 * far * near * nf;
    return o;
  },
  lookAt(eye, center, up) {
    const z = norm3(sub3(eye, center));
    const x = norm3(cross3(up, z));
    const y = cross3(z, x);
    const o = new Float32Array(16);
    o[0] = x[0]; o[4] = x[1]; o[8] = x[2]; o[12] = -dot3(x, eye);
    o[1] = y[0]; o[5] = y[1]; o[9] = y[2]; o[13] = -dot3(y, eye);
    o[2] = z[0]; o[6] = z[1]; o[10] = z[2]; o[14] = -dot3(z, eye);
    o[15] = 1;
    return o;
  },
  // Normal matrix = transpose(inverse(upper-left 3x3 of model)). Models here are
  // a uniform scale+translate, so the upper-left is orthogonal up to scale; pass
  // the model's 3x3 (good enough for non-skewed transforms).
  normalMat3(m) {
    return new Float32Array([m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]]);
  },
};
const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross3 = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm3 = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

// ---------------------------------------------------------------------------
// Compact GGX PBR shaders (WebGL2 / GLSL ES 3.00).
// ---------------------------------------------------------------------------

const VERT_SRC = `#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vNormal = normalize(uNormalMat * aNormal);
  vUv = aUv;
  gl_Position = uProj * uView * wp;
}`;

const FRAG_SRC = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;

uniform vec3 uCamPos;
uniform vec3 uLightDir;      // direction TOWARDS the light
uniform vec3 uLightColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;

uniform vec3  uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uOpacity;
uniform vec3  uEmissive;
uniform float uOcclusion;    // constant AO when no map
uniform int   uDoubleSided;
uniform int   uAlphaCutoffOn;
uniform float uAlphaCutoff;

uniform int uHasBaseMap, uHasNormalMap, uHasMrMap, uHasAoMap, uHasEmissiveMap;
uniform sampler2D uBaseMap, uNormalMap, uMrMap, uAoMap, uEmissiveMap;

const float PI = 3.14159265359;

float distGGX(float NdotH, float a) {
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-7);
}
float smithG(float NdotV, float NdotL, float rough) {
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  float gv = NdotV / (NdotV * (1.0 - k) + k);
  float gl = NdotL / (NdotL * (1.0 - k) + k);
  return gv * gl;
}
vec3 fresnel(float c, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0); }
vec3 toLinear(vec3 c) { return pow(c, vec3(2.2)); }

// Tangent-free normal mapping (cotangent frame from screen-space derivatives).
vec3 perturbNormal(vec3 N, vec2 mapN_xy, float mapN_z) {
  vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
  vec2 duv1 = dFdx(vUv), duv2 = dFdy(vUv);
  vec3 dp2perp = cross(dp2, N), dp1perp = cross(N, dp1);
  vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
  mat3 TBN = mat3(T * invmax, B * invmax, N);
  return normalize(TBN * vec3(mapN_xy, mapN_z));
}

void main() {
  vec3 base = uBaseColor;
  float alpha = uOpacity;
  if (uHasBaseMap == 1) { vec4 t = texture(uBaseMap, vUv); base *= toLinear(t.rgb); alpha *= t.a; }
  if (uAlphaCutoffOn == 1 && alpha < uAlphaCutoff) discard;

  float metallic = uMetallic, rough = clamp(uRoughness, 0.04, 1.0);
  if (uHasMrMap == 1) { vec4 mr = texture(uMrMap, vUv); rough = clamp(rough * mr.g, 0.04, 1.0); metallic *= mr.b; }
  float ao = uOcclusion;
  if (uHasAoMap == 1) ao *= texture(uAoMap, vUv).r;

  vec3 N = normalize(vNormal);
  if (uDoubleSided == 1 && !gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos - vWorldPos);
  if (uHasNormalMap == 1) {
    vec3 m = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;
    N = perturbNormal(N, m.xy, m.z);
  }

  vec3 F0 = mix(vec3(0.04), base, metallic);
  vec3 L = normalize(uLightDir);
  vec3 H = normalize(V + L);
  float NdotL = max(dot(N, L), 0.0);
  float NdotV = max(dot(N, V), 1e-3);
  float NdotH = max(dot(N, H), 0.0);
  float HdotV = max(dot(H, V), 0.0);

  float D = distGGX(NdotH, rough * rough);
  float G = smithG(NdotV, NdotL, rough);
  vec3  Fr = fresnel(HdotV, F0);
  vec3 spec = (D * G * Fr) / max(4.0 * NdotV * NdotL, 1e-3);
  vec3 kd = (1.0 - Fr) * (1.0 - metallic);
  vec3 direct = (kd * base / PI + spec) * uLightColor * NdotL;

  // Cheap hemispheric ambient stands in for IBL: sky above, ground below.
  float hemi = 0.5 + 0.5 * N.y;
  vec3 ambient = mix(uAmbientGround, uAmbientSky, hemi) * base * ao * (1.0 - 0.4 * metallic);

  vec3 emissive = uEmissive;
  if (uHasEmissiveMap == 1) emissive *= toLinear(texture(uEmissiveMap, vUv).rgb);

  vec3 color = direct + ambient + emissive;
  color = color / (color + vec3(1.0));        // Reinhard tonemap
  color = pow(color, vec3(1.0 / 2.2));         // gamma encode
  fragColor = vec4(color, alpha);
}`;

// ---------------------------------------------------------------------------
// Heap-view + dtype helpers.
// ---------------------------------------------------------------------------

// Build a typed-array VIEW into the live WASM heap for a zero-copy descriptor
// {ptr, length, dtype}. The view is valid only until the heap grows — read it
// (e.g. gl.bufferData) before any call that could allocate WASM memory.
function heapView(native, desc) {
  const buf = native.HEAPU8.buffer;
  const ptr = Number(desc.ptr);
  const n = desc.length;
  switch (desc.dtype) {
    case 'f32': return new Float32Array(buf, ptr, n);
    case 'f64': return new Float64Array(buf, ptr, n);
    case 'u32': return new Uint32Array(buf, ptr, n);
    case 'i32': return new Int32Array(buf, ptr, n);
    case 'u16': return new Uint16Array(buf, ptr, n);
    case 'snorm16': return new Int16Array(buf, ptr, n);
    case 'snorm8': return new Int8Array(buf, ptr, n);
    default: return new Uint8Array(buf, ptr, desc.byteLength);
  }
}

// dtype -> WebGL vertex-attribute type + whether it is normalized.
function glAttrType(gl, dtype) {
  switch (dtype) {
    case 'f32': return { type: gl.FLOAT, normalized: false };
    case 'snorm8': return { type: gl.BYTE, normalized: true };
    case 'snorm16': return { type: gl.SHORT, normalized: true };
    case 'u8': return { type: gl.UNSIGNED_BYTE, normalized: true };
    default: return { type: gl.FLOAT, normalized: false };
  }
}

const fmtMB = (b) => (b / 1048576).toFixed(1) + ' MB';

// ---------------------------------------------------------------------------
// The renderer.
// ---------------------------------------------------------------------------

export class StreamingUSDRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = null;
    this.prog = null;
    this.loc = {};        // uniform locations
    this.loader = null;   // TinyUSDZLoader (owns the WASM Module)
    this.native = null;   // Emscripten Module (HEAPU8 etc.)
    this.drawables = [];  // { vao, count, indexType, material, bbox }
    this.glTextures = []; // GL texture per RenderScene image id (or null)
    this.bbox = null;
    this._raf = 0;
    this._memCb = null;
    this._memTimer = 0;
    // Orbit camera state.
    this.cam = { az: 0.6, el: 0.5, dist: 3, target: [0, 0, 0], fov: 50 * Math.PI / 180 };
    this._inputSize = 0;
  }

  // ---- lifecycle -----------------------------------------------------------

  async init() {
    const gl = this.canvas.getContext('webgl2', { antialias: true, alpha: false });
    if (!gl) throw new Error('WebGL2 is required for the streaming renderer.');
    this.gl = gl;
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0.09, 0.10, 0.12, 1.0);
    this.prog = this._buildProgram(VERT_SRC, FRAG_SRC);
    gl.useProgram(this.prog);
    for (const u of ['uModel', 'uView', 'uProj', 'uNormalMat', 'uCamPos', 'uLightDir',
      'uLightColor', 'uAmbientSky', 'uAmbientGround', 'uBaseColor', 'uMetallic',
      'uRoughness', 'uOpacity', 'uEmissive', 'uOcclusion', 'uDoubleSided',
      'uAlphaCutoffOn', 'uAlphaCutoff', 'uHasBaseMap', 'uHasNormalMap', 'uHasMrMap',
      'uHasAoMap', 'uHasEmissiveMap', 'uBaseMap', 'uNormalMap', 'uMrMap', 'uAoMap',
      'uEmissiveMap']) {
      this.loc[u] = gl.getUniformLocation(this.prog, u);
    }
    // Fixed sampler units.
    gl.uniform1i(this.loc.uBaseMap, 0);
    gl.uniform1i(this.loc.uNormalMap, 1);
    gl.uniform1i(this.loc.uMrMap, 2);
    gl.uniform1i(this.loc.uAoMap, 3);
    gl.uniform1i(this.loc.uEmissiveMap, 4);

    this.loader = new TinyUSDZLoader();
    await this.loader.init();
    this.native = this.loader.native_;
    this._installCameraControls();
    this._startRenderLoop();
    return this;
  }

  heapBytes() { return this.native ? this.native.HEAPU8.buffer.byteLength : 0; }

  onMemory(cb) { this._memCb = cb; }

  // ---- loading (the low-memory pipeline) -----------------------------------

  // Load a USD/USDZ from bytes. Streams it into the heap, builds the render
  // scene (textures left encoded), uploads every mesh + texture to the GPU, then
  // frees the WASM render scene. Returns { meshes, textures, materials, memory }.
  async loadBytes(bytes, filename = 'scene.usdz') {
    const gl = this.gl;
    const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    this._inputSize = u8.length;
    this._disposeScene();

    const mem = { input: u8.length, phases: [] };
    const snap = (label) => {
      const p = { label, heapReserved: this.heapBytes() };
      try {
        const s = this.usd ? this.usd.getMemoryStats() : null;
        p.renderBuffers = s ? s.bufferMemoryBytes : 0;
        p.assetCache = s ? s.assetCacheSizeBytes : 0;
      } catch (_) { p.renderBuffers = 0; p.assetCache = 0; }
      mem.phases.push(p);
      this._emitMemory();
      return p;
    };

    // Fresh native loader; keep textures ENCODED in the heap (JS decodes them),
    // so decoded RGBA never lands in the WASM heap.
    this.usd = new this.native.TinyUSDZLoaderNative();
    if (typeof this.usd.setLoadTextureInNative === 'function') {
      this.usd.setLoadTextureInNative(false);
    }
    snap('baseline');

    // Build the render scene. NOTE: loadFromBinary currently converts the WHOLE
    // scene eagerly (parse Stage + convert to RenderScene), so the transient peak
    // is several× the input — see the memory panel. The streaming wins below
    // (encoded textures, zero-copy upload, reset) bound the RESIDENT footprint,
    // not this conversion peak; bounding the peak to ~input needs an incremental
    // converter (tracked in the header notes).
    if (!this.usd.loadFromBinary(u8, filename)) {
      const err = this.usd.error ? this.usd.error() : 'unknown';
      throw new Error('Failed to load USD: ' + err);
    }
    snap('render scene built (peak)');

    const numMeshes = this.usd.numMeshes();
    const numMaterials = this.usd.numMaterials();
    const numImages = this.usd.numImages();

    // 3) Upload every texture (decode encoded bytes in JS, off the WASM heap).
    this.glTextures = new Array(numImages).fill(null);
    const colorImages = this._colorImageIds(numMaterials); // which images are sRGB
    for (let i = 0; i < numImages; i++) {
      // eslint-disable-next-line no-await-in-loop
      this.glTextures[i] = await this._uploadImage(i, colorImages.has(i));
    }
    snap('textures uploaded to GPU');

    // 4) Upload every mesh straight from the heap to GL buffers.
    this.drawables = [];
    this.bbox = null;
    for (let i = 0; i < numMeshes; i++) {
      const d = this._uploadMesh(i, numMaterials);
      if (d) this.drawables.push(d);
    }
    snap('meshes uploaded to GPU');

    // 5) Free the entire WASM-side render scene + input. The GPU keeps the data.
    this.usd.reset();
    snap('WASM render scene freed (reset)');

    this._frameCamera();
    mem.peakReserved = Math.max(...mem.phases.map((p) => p.heapReserved));
    mem.summary = {
      inputMB: mem.input / 1048576,
      peakHeapMB: mem.peakReserved / 1048576,
      residentBuffersMB: 0, // after reset, the working set is on the GPU
      ratio: mem.peakReserved / Math.max(1, mem.input),
    };
    this._lastMemory = mem;
    this._emitMemory();
    return { meshes: this.drawables.length, textures: numImages, materials: numMaterials, memory: mem };
  }

  // Parse getMaterial(matId) (a JSON string) into a normalized shader record.
  // tydra fills `surfaceShader` (UsdPreviewSurface-style) for both
  // UsdPreviewSurface AND MaterialX; an `openPBR` block (base_*) is mapped onto
  // the same fields when no PreviewSurface is present. Texture bindings are
  // RenderScene texture ids (resolve to an image via getTexture().textureImageId).
  _getShader(matId) {
    let raw;
    try { raw = this.usd.getMaterial(matId); } catch (_) { return null; }
    if (!raw || !raw.data) return null;
    let obj;
    try { obj = JSON.parse(raw.data); } catch (_) { return null; }
    const ss = obj.surfaceShader;
    if (ss && obj.hasUsdPreviewSurface !== false) return ss;
    const o = obj.openPBR || obj.openpbr;
    if (o) {
      const t = (v) => (v && typeof v === 'object' && 'textureId' in v) ? v.textureId : undefined;
      return {
        diffuseColor: o.base_color && o.base_color.value || o.base_color || [0.8, 0.8, 0.8],
        metallic: o.base_metalness && o.base_metalness.value != null ? o.base_metalness.value : (o.base_metalness ?? 0),
        roughness: o.specular_roughness && o.specular_roughness.value != null ? o.specular_roughness.value : (o.specular_roughness ?? 0.5),
        opacity: o.geometry_opacity ?? 1,
        emissiveColor: o.emission_color && o.emission_color.value || o.emission_color || [0, 0, 0],
        diffuseColorTextureId: t(o.base_color),
        metallicTextureId: t(o.base_metalness),
        roughnessTextureId: t(o.specular_roughness),
        normalTextureId: t(o.geometry_normal),
        emissiveColorTextureId: t(o.emission_color),
      };
    }
    return ss || null;
  }

  // Which image ids are sampled as color (sRGB) vs data (linear), from the
  // materials' base-color / emissive bindings.
  _colorImageIds(numMaterials) {
    const color = new Set();
    for (let m = 0; m < numMaterials; m++) {
      const s = this._getShader(m);
      if (!s) continue;
      for (const k of ['diffuseColorTextureId', 'emissiveColorTextureId']) {
        const tid = s[k];
        if (typeof tid === 'number' && tid >= 0) {
          const imgId = this._textureImageId(tid);
          if (imgId >= 0) color.add(imgId);
        }
      }
    }
    return color;
  }

  _textureImageId(texId) {
    try {
      const t = this.usd.getTexture(texId);
      return t && typeof t.textureImageId === 'number' ? t.textureImageId : -1;
    } catch (_) { return -1; }
  }

  // Decode + upload one image to a GL texture. Encoded images (decoded=false)
  // are decoded with createImageBitmap (off the WASM heap); already-decoded
  // images are uploaded straight from the heap view.
  async _uploadImage(imgId, srgb) {
    const gl = this.gl;
    let desc;
    try { desc = this.usd.getImagePtr(imgId); } catch (_) { return null; }
    if (!desc || !desc.byteLength) return null;

    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    const internal = srgb ? gl.SRGB8_ALPHA8 : gl.RGBA8;

    if (desc.decoded) {
      // Raw pixels in the heap. Normalize to RGBA8.
      const view = new Uint8Array(this.native.HEAPU8.buffer, Number(desc.ptr), desc.byteLength);
      const rgba = this._toRGBA8(view, desc.width, desc.height, desc.channels);
      gl.texImage2D(gl.TEXTURE_2D, 0, internal, desc.width, desc.height, 0,
        gl.RGBA, gl.UNSIGNED_BYTE, rgba);
    } else {
      // Encoded bytes (PNG/JPEG/...). Copy out of the heap into a Blob, decode
      // off-heap via the browser, upload. The heap copy is transient (one image).
      const view = new Uint8Array(this.native.HEAPU8.buffer, Number(desc.ptr), desc.byteLength);
      const bytes = view.slice(); // detach from heap before any async/alloc
      let bmp;
      try {
        bmp = await createImageBitmap(new Blob([bytes]), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
      } catch (e) {
        gl.deleteTexture(tex);
        return null;
      }
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, internal, gl.RGBA, gl.UNSIGNED_BYTE, bmp);
      bmp.close && bmp.close();
    }
    gl.generateMipmap(gl.TEXTURE_2D);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    return tex;
  }

  _toRGBA8(src, w, h, channels) {
    if (channels === 4) return src;
    const n = w * h, out = new Uint8Array(n * 4);
    for (let i = 0; i < n; i++) {
      const r = src[i * channels];
      out[i * 4 + 0] = r;
      out[i * 4 + 1] = channels >= 2 ? src[i * channels + 1] : r;
      out[i * 4 + 2] = channels >= 3 ? src[i * channels + 2] : r;
      out[i * 4 + 3] = 255;
    }
    return out;
  }

  // Upload one mesh's geometry (zero-copy heap views -> GL buffers) and resolve
  // its material into a small GPU-ready record.
  _uploadMesh(meshId, numMaterials) {
    const gl = this.gl;
    let m;
    try { m = this.usd.getMeshPtr(meshId); } catch (_) { return null; }
    if (!m || !m.points || !m.points.length) return null;

    const vao = gl.createVertexArray();
    gl.bindVertexArray(vao);

    // Positions (attrib 0). Accumulate the scene bbox while the view is live.
    this._uploadAttrib(0, m.points, 3);
    this._growBBox(heapView(this.native, m.points));

    // Normals (attrib 1) — from the mesh, or computed if absent.
    if (m.normals && m.normals.length) {
      this._uploadAttrib(1, m.normals, 3);
    } else {
      const normals = this._computeNormals(m);
      const nb = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, nb);
      gl.bufferData(gl.ARRAY_BUFFER, normals, gl.STATIC_DRAW);
      gl.enableVertexAttribArray(1);
      gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);
    }

    // UV0 (attrib 2) — optional.
    const uv = m.uv0 && m.uv0.length ? m.uv0 : (m.uvSets && m.uvSets.uv0);
    if (uv && uv.length) {
      this._uploadAttrib(2, uv, 2);
    } else {
      gl.disableVertexAttribArray(2);
      gl.vertexAttrib2f(2, 0, 0);
    }

    // Indices — triangulate if the mesh is not already triangle-only.
    let count, indexType = gl.UNSIGNED_INT, mode = 'elements';
    if (m.indices && m.indices.length) {
      let idx = heapView(this.native, m.indices); // u32 view
      if (!m.triangulated && m.faceVertexCounts && m.faceVertexCounts.length) {
        idx = this._triangulate(idx, heapView(this.native, m.faceVertexCounts));
      }
      const ib = gl.createBuffer();
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
      count = idx.length;
    } else {
      mode = 'arrays';
      count = m.points.length / 3;
    }

    gl.bindVertexArray(null);
    const material = this._resolveMaterial(m.materialId, numMaterials);
    return { vao, count, indexType, mode, material, doubleSided: !!m.doubleSided };
  }

  _uploadAttrib(location, desc, comps) {
    const gl = this.gl;
    const view = heapView(this.native, desc);
    const b = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, b);
    gl.bufferData(gl.ARRAY_BUFFER, view, gl.STATIC_DRAW); // GL copies into GPU now
    const at = glAttrType(gl, desc.dtype);
    gl.enableVertexAttribArray(location);
    gl.vertexAttribPointer(location, desc.comps || comps, at.type, at.normalized, 0, 0);
  }

  _growBBox(positions) {
    let bb = this.bbox;
    if (!bb) { bb = this.bbox = { min: [1e30, 1e30, 1e30], max: [-1e30, -1e30, -1e30] }; }
    for (let i = 0; i < positions.length; i += 3) {
      for (let c = 0; c < 3; c++) {
        const v = positions[i + c];
        if (v < bb.min[c]) bb.min[c] = v;
        if (v > bb.max[c]) bb.max[c] = v;
      }
    }
  }

  _triangulate(indices, fvc) {
    const tris = [];
    let base = 0;
    for (let f = 0; f < fvc.length; f++) {
      const n = fvc[f];
      for (let k = 2; k < n; k++) {
        tris.push(indices[base], indices[base + k - 1], indices[base + k]);
      }
      base += n;
    }
    return new Uint32Array(tris);
  }

  _computeNormals(m) {
    const pos = heapView(this.native, m.points);
    const nv = pos.length / 3;
    const out = new Float32Array(pos.length);
    let idx = m.indices && m.indices.length ? heapView(this.native, m.indices) : null;
    if (idx && !m.triangulated && m.faceVertexCounts && m.faceVertexCounts.length) {
      idx = this._triangulate(idx, heapView(this.native, m.faceVertexCounts));
    }
    const tri = idx || (() => { const a = new Uint32Array(nv); for (let i = 0; i < nv; i++) a[i] = i; return a; })();
    for (let t = 0; t < tri.length; t += 3) {
      const a = tri[t] * 3, b = tri[t + 1] * 3, c = tri[t + 2] * 3;
      const e1 = [pos[b] - pos[a], pos[b + 1] - pos[a + 1], pos[b + 2] - pos[a + 2]];
      const e2 = [pos[c] - pos[a], pos[c + 1] - pos[a + 1], pos[c + 2] - pos[a + 2]];
      const fn = cross3(e1, e2);
      for (const vi of [a, b, c]) { out[vi] += fn[0]; out[vi + 1] += fn[1]; out[vi + 2] += fn[2]; }
    }
    for (let i = 0; i < out.length; i += 3) {
      const l = Math.hypot(out[i], out[i + 1], out[i + 2]) || 1;
      out[i] /= l; out[i + 1] /= l; out[i + 2] /= l;
    }
    return out;
  }

  // Resolve getMaterial(matId) into shader-ready params + bound GL textures.
  // tydra fills the same UsdPreviewSurface-style record for UsdPreviewSurface
  // and MaterialX, so both shade through this path.
  _resolveMaterial(matId, numMaterials) {
    const def = {
      baseColor: [0.8, 0.8, 0.8], metallic: 0.0, roughness: 0.5, opacity: 1.0,
      emissive: [0, 0, 0], occlusion: 1.0, alphaCutoff: -1,
      baseMap: null, normalMap: null, mrMap: null, aoMap: null, emissiveMap: null,
    };
    if (typeof matId !== 'number' || matId < 0 || matId >= numMaterials) return def;
    const mat = this._getShader(matId);
    if (!mat) return def;

    if (Array.isArray(mat.diffuseColor)) def.baseColor = mat.diffuseColor.slice(0, 3);
    if (typeof mat.metallic === 'number') def.metallic = mat.metallic;
    if (typeof mat.roughness === 'number') def.roughness = mat.roughness;
    if (typeof mat.opacity === 'number') def.opacity = mat.opacity;
    if (Array.isArray(mat.emissiveColor)) def.emissive = mat.emissiveColor.slice(0, 3);
    if (typeof mat.occlusion === 'number' && mat.occlusion > 0) def.occlusion = mat.occlusion;
    if (typeof mat.opacityThreshold === 'number' && mat.opacityThreshold > 0) {
      def.alphaCutoff = mat.opacityThreshold;
    }
    const texOf = (tid) => {
      if (typeof tid !== 'number' || tid < 0) return null;
      const imgId = this._textureImageId(tid);
      return imgId >= 0 ? this.glTextures[imgId] || null : null;
    };
    def.baseMap = texOf(mat.diffuseColorTextureId);
    def.normalMap = texOf(mat.normalTextureId);
    def.aoMap = texOf(mat.occlusionTextureId);
    def.emissiveMap = texOf(mat.emissiveColorTextureId);
    // Metallic/roughness may be one packed ORM map or two scalar maps; both work
    // because the shader reads .g (roughness) and .b (metallic), and a grayscale
    // scalar map has r==g==b.
    def.mrMap = texOf(mat.roughnessTextureId) || texOf(mat.metallicTextureId);
    return def;
  }

  // ---- rendering -----------------------------------------------------------

  _frameCamera() {
    if (!this.bbox || this.bbox.min[0] > this.bbox.max[0]) return;
    const c = [0, 1, 2].map((i) => (this.bbox.min[i] + this.bbox.max[i]) / 2);
    const r = Math.max(1e-3, 0.5 * Math.hypot(
      this.bbox.max[0] - this.bbox.min[0],
      this.bbox.max[1] - this.bbox.min[1],
      this.bbox.max[2] - this.bbox.min[2]));
    this.cam.target = c;
    this.cam.dist = r / Math.sin(this.cam.fov / 2) * 1.1;
    this._near = Math.max(1e-3, this.cam.dist - r * 2);
    this._far = this.cam.dist + r * 2;
  }

  _camEye() {
    const { az, el, dist, target } = this.cam;
    const ce = Math.cos(el), se = Math.sin(el);
    return [target[0] + dist * ce * Math.sin(az), target[1] + dist * se, target[2] + dist * ce * Math.cos(az)];
  }

  _startRenderLoop() {
    const loop = () => { this._raf = requestAnimationFrame(loop); this._render(); };
    this._raf = requestAnimationFrame(loop);
  }

  _render() {
    const gl = this.gl, canvas = this.canvas;
    const w = canvas.clientWidth | 0, h = canvas.clientHeight | 0;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (!this.drawables.length) return;

    const eye = this._camEye();
    const proj = M.perspective(this.cam.fov, (canvas.width || 1) / (canvas.height || 1),
      this._near || 0.01, this._far || 1000);
    const view = M.lookAt(eye, this.cam.target, [0, 1, 0]);
    const model = M.ident();

    gl.useProgram(this.prog);
    gl.uniformMatrix4fv(this.loc.uModel, false, model);
    gl.uniformMatrix4fv(this.loc.uView, false, view);
    gl.uniformMatrix4fv(this.loc.uProj, false, proj);
    gl.uniformMatrix3fv(this.loc.uNormalMat, false, M.normalMat3(model));
    gl.uniform3fv(this.loc.uCamPos, eye);
    gl.uniform3fv(this.loc.uLightDir, norm3([0.4, 0.8, 0.5]));
    gl.uniform3fv(this.loc.uLightColor, [3.0, 2.9, 2.7]);
    gl.uniform3fv(this.loc.uAmbientSky, [0.35, 0.40, 0.48]);
    gl.uniform3fv(this.loc.uAmbientGround, [0.12, 0.11, 0.10]);

    for (const d of this.drawables) {
      const mt = d.material;
      gl.uniform3fv(this.loc.uBaseColor, mt.baseColor);
      gl.uniform1f(this.loc.uMetallic, mt.metallic);
      gl.uniform1f(this.loc.uRoughness, mt.roughness);
      gl.uniform1f(this.loc.uOpacity, mt.opacity);
      gl.uniform3fv(this.loc.uEmissive, mt.emissive);
      gl.uniform1f(this.loc.uOcclusion, mt.occlusion);
      gl.uniform1i(this.loc.uDoubleSided, d.doubleSided ? 1 : 0);
      gl.uniform1i(this.loc.uAlphaCutoffOn, mt.alphaCutoff > 0 ? 1 : 0);
      gl.uniform1f(this.loc.uAlphaCutoff, mt.alphaCutoff > 0 ? mt.alphaCutoff : 0.5);
      this._bindMap(0, mt.baseMap, this.loc.uHasBaseMap);
      this._bindMap(1, mt.normalMap, this.loc.uHasNormalMap);
      this._bindMap(2, mt.mrMap, this.loc.uHasMrMap);
      this._bindMap(3, mt.aoMap, this.loc.uHasAoMap);
      this._bindMap(4, mt.emissiveMap, this.loc.uHasEmissiveMap);
      gl.bindVertexArray(d.vao);
      if (d.mode === 'elements') gl.drawElements(gl.TRIANGLES, d.count, d.indexType, 0);
      else gl.drawArrays(gl.TRIANGLES, 0, d.count);
    }
    gl.bindVertexArray(null);
  }

  _bindMap(unit, tex, hasLoc) {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex || null);
    gl.uniform1i(hasLoc, tex ? 1 : 0);
  }

  // ---- camera controls -----------------------------------------------------

  _installCameraControls() {
    const el = this.canvas;
    let drag = false, lx = 0, ly = 0;
    el.addEventListener('pointerdown', (e) => { drag = true; lx = e.clientX; ly = e.clientY; el.setPointerCapture(e.pointerId); });
    el.addEventListener('pointerup', (e) => { drag = false; try { el.releasePointerCapture(e.pointerId); } catch (_) {} });
    el.addEventListener('pointermove', (e) => {
      if (!drag) return;
      this.cam.az -= (e.clientX - lx) * 0.01;
      this.cam.el = Math.max(-1.5, Math.min(1.5, this.cam.el + (e.clientY - ly) * 0.01));
      lx = e.clientX; ly = e.clientY;
    });
    el.addEventListener('wheel', (e) => {
      e.preventDefault();
      this.cam.dist *= Math.exp(e.deltaY * 0.001);
    }, { passive: false });
  }

  // ---- memory reporting ----------------------------------------------------

  // Start polling live heap size at `intervalMs` and report through onMemory().
  startMemoryPolling(intervalMs = 500) {
    this.stopMemoryPolling();
    this._memTimer = setInterval(() => this._emitMemory(), intervalMs);
  }
  stopMemoryPolling() { if (this._memTimer) { clearInterval(this._memTimer); this._memTimer = 0; } }

  _emitMemory() {
    if (!this._memCb) return;
    let renderBuffers = 0;
    try { const s = this.usd && this.usd.getMemoryStats(); if (s) renderBuffers = s.bufferMemoryBytes; } catch (_) {}
    this._memCb({
      inputBytes: this._inputSize,
      heapReserved: this.heapBytes(),
      renderBuffers,
      last: this._lastMemory || null,
    });
  }

  // ---- teardown ------------------------------------------------------------

  _disposeScene() {
    const gl = this.gl;
    for (const d of this.drawables) { try { gl.deleteVertexArray(d.vao); } catch (_) {} }
    for (const t of this.glTextures) { if (t) { try { gl.deleteTexture(t); } catch (_) {} } }
    this.drawables = []; this.glTextures = []; this.bbox = null;
    if (this.usd) { try { this.usd.reset(); this.usd.delete(); } catch (_) {} this.usd = null; }
  }

  dispose() {
    cancelAnimationFrame(this._raf);
    this.stopMemoryPolling();
    this._disposeScene();
  }

  // ---- GL program helper ---------------------------------------------------

  _buildProgram(vsrc, fsrc) {
    const gl = this.gl;
    const sh = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        throw new Error('Shader compile error: ' + gl.getShaderInfoLog(s));
      }
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, vsrc));
    gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fsrc));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      throw new Error('Program link error: ' + gl.getProgramInfoLog(p));
    }
    return p;
  }
}

// ---------------------------------------------------------------------------
// Demo bootstrap: wire a renderer to a canvas, a file picker / drag-drop, and a
// live memory panel. Call from streaming.html.
// ---------------------------------------------------------------------------

export async function mountStreamingDemo(opts = {}) {
  const canvas = opts.canvas || document.getElementById('gl');
  const panel = opts.panel || document.getElementById('mem-panel');
  const status = opts.status || document.getElementById('status');
  const fileInput = opts.fileInput || document.getElementById('file');

  const renderer = new StreamingUSDRenderer(canvas);
  await renderer.init();

  const setStatus = (s) => { if (status) status.textContent = s; };
  const renderPanel = (m) => {
    if (!panel) return;
    const ratio = m.inputBytes ? (m.heapReserved / m.inputBytes).toFixed(2) : '–';
    const rows = [
      ['Input USD', fmtMB(m.inputBytes)],
      ['WASM heap (reserved peak)', fmtMB(m.heapReserved)],
      ['WASM render buffers (live)', fmtMB(m.renderBuffers)],
      ['Heap / input', ratio + '×'],
    ];
    let html = '<table>' + rows.map(([k, v]) =>
      `<tr><td>${k}</td><td style="text-align:right">${v}</td></tr>`).join('') + '</table>';
    if (m.last && m.last.phases) {
      html += '<div class="phases"><b>Load phases (heap reserved / live buffers)</b><table>' +
        m.last.phases.map((p) =>
          `<tr><td>${p.label}</td><td style="text-align:right">${fmtMB(p.heapReserved)}</td>` +
          `<td style="text-align:right">${fmtMB(p.renderBuffers)}</td></tr>`).join('') +
        '</table></div>';
      const s = m.last.summary;
      if (s) html += `<div class="target">Transient conversion peak ${s.peakHeapMB.toFixed(1)} MB ` +
        `for ${s.inputMB.toFixed(1)} MB input → <b>${s.ratio.toFixed(2)}×</b>. ` +
        `After upload+reset the render data lives on the GPU and the WASM render ` +
        `working set is freed. Reaching ≈1× (the input-size target) needs the ` +
        `incremental converter — the eager loadFromBinary builds the whole scene at once.</div>`;
    }
    panel.innerHTML = html;
  };
  renderer.onMemory(renderPanel);
  renderer.startMemoryPolling(500);

  async function loadFile(file) {
    setStatus(`Loading ${file.name} (${fmtMB(file.size)})…`);
    try {
      const buf = new Uint8Array(await file.arrayBuffer());
      const t0 = performance.now();
      const r = await renderer.loadBytes(buf, file.name);
      const dt = (performance.now() - t0).toFixed(0);
      setStatus(`${file.name}: ${r.meshes} meshes, ${r.textures} textures, ` +
        `${r.materials} materials in ${dt} ms — peak WASM heap ` +
        `${r.memory.summary.peakHeapMB.toFixed(1)} MB (${r.memory.summary.ratio.toFixed(2)}× input)`);
    } catch (e) {
      setStatus('Error: ' + (e && e.message));
      console.error(e);
    }
  }

  if (fileInput) {
    fileInput.addEventListener('change', (e) => { if (e.target.files[0]) loadFile(e.target.files[0]); });
  }
  // Drag-and-drop onto the canvas.
  const stop = (e) => { e.preventDefault(); e.stopPropagation(); };
  for (const ev of ['dragenter', 'dragover', 'dragleave', 'drop']) canvas.addEventListener(ev, stop);
  canvas.addEventListener('drop', (e) => { const f = e.dataTransfer.files[0]; if (f) loadFile(f); });

  // Optional initial model.
  if (opts.url) {
    setStatus(`Fetching ${opts.url}…`);
    try {
      const resp = await fetch(opts.url);
      const buf = new Uint8Array(await resp.arrayBuffer());
      const name = opts.url.split('/').pop();
      const r = await renderer.loadBytes(buf, name);
      setStatus(`${name}: ${r.meshes} meshes, ${r.textures} textures — peak WASM heap ` +
        `${r.memory.summary.peakHeapMB.toFixed(1)} MB (${r.memory.summary.ratio.toFixed(2)}× input)`);
    } catch (e) { setStatus('Drop a .usdz/.usdc file to render. ' + (e && e.message || '')); }
  } else {
    setStatus('Drop a .usdz / .usdc / .usda file onto the canvas, or pick one.');
  }

  return renderer;
}

export default StreamingUSDRenderer;
