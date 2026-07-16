import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const state = {
  mode: 'precomputed',
  preset: 'medium',
  normalScale: 1,
  mesh: null,
  stats: {}
};

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101318);

const camera = new THREE.PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.05, 200);
camera.position.set(3.2, 2.4, 3.4);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0, 0);
controls.enableDamping = true;

scene.add(new THREE.HemisphereLight(0xbfd8ff, 0x1d2530, 1.6));
const key = new THREE.DirectionalLight(0xffffff, 2.2);
key.position.set(4, 5, 3);
scene.add(key);

const statsEl = document.getElementById('stats');
const modeEl = document.getElementById('mode');
const presetEl = document.getElementById('preset');
const scaleEl = document.getElementById('scale');
const rebuildEl = document.getElementById('rebuild');

const material = new THREE.ShaderMaterial({
  uniforms: {
    uMode: { value: 0 },
    uNormalScale: { value: 1 },
    uLightDir: { value: new THREE.Vector3(0.55, 0.72, 0.42).normalize() }
  },
  vertexShader: `
    attribute vec4 tangent;
    varying vec3 vPos;
    varying vec3 vNormal;
    varying vec4 vTangent;
    varying vec2 vUv;
    void main() {
      vec4 wp = modelMatrix * vec4(position, 1.0);
      vPos = wp.xyz;
      vNormal = normalize(mat3(modelMatrix) * normal);
      vTangent = vec4(normalize(mat3(modelMatrix) * tangent.xyz), tangent.w);
      vUv = uv;
      gl_Position = projectionMatrix * viewMatrix * wp;
    }
  `,
  fragmentShader: `
    precision highp float;
    uniform int uMode;
    uniform float uNormalScale;
    uniform vec3 uLightDir;
    varying vec3 vPos;
    varying vec3 vNormal;
    varying vec4 vTangent;
    varying vec2 vUv;

    vec3 proceduralNormal(vec2 uv) {
      float a = sin(uv.x * 85.0) * sin(uv.y * 77.0);
      float b = sin((uv.x + uv.y) * 46.0);
      vec2 slope = vec2(a, b) * 0.42 * uNormalScale;
      return normalize(vec3(slope, 1.0));
    }

    mat3 precomputedTBN() {
      vec3 n = normalize(vNormal);
      vec3 t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
      vec3 b = normalize(cross(n, t) * vTangent.w);
      return mat3(t, b, n);
    }

    mat3 derivativeTBN() {
      vec3 dp1 = dFdx(vPos);
      vec3 dp2 = dFdy(vPos);
      vec2 duv1 = dFdx(vUv);
      vec2 duv2 = dFdy(vUv);
      vec3 n = normalize(vNormal);
      vec3 t = normalize(dp1 * duv2.y - dp2 * duv1.y);
      vec3 b = normalize(-dp1 * duv2.x + dp2 * duv1.x);
      t = normalize(t - n * dot(n, t));
      b = normalize(b - n * dot(n, b) - t * dot(t, b));
      return mat3(t, b, n);
    }

    vec3 shade(vec3 n) {
      vec3 l = normalize(uLightDir);
      float ndl = max(dot(n, l), 0.0);
      vec3 base = mix(vec3(0.13, 0.34, 0.50), vec3(0.68, 0.82, 0.78), vUv.y);
      vec3 grid = vec3(step(0.975, max(fract(vUv.x * 16.0), fract(vUv.y * 16.0))));
      return base * (0.18 + 0.82 * ndl) + grid * 0.12;
    }

    void main() {
      vec3 mapN = proceduralNormal(vUv);
      vec3 preN = normalize(precomputedTBN() * mapN);
      vec3 derN = normalize(derivativeTBN() * mapN);
      vec3 n = normalize(vNormal);
      if (uMode == 0) n = preN;
      else if (uMode == 1) n = derN;
      else if (uMode == 2) n = gl_FragCoord.x < (0.5 * float(${Math.max(1, window.innerWidth)})) ? preN : derN;
      gl_FragColor = vec4(shade(n), 1.0);
    }
  `,
  extensions: { derivatives: true }
});

function modeCode(mode) {
  if (mode === 'derivative') return 1;
  if (mode === 'split') return 2;
  if (mode === 'normal') return 3;
  return 0;
}

function presetSize(preset) {
  if (preset === 'large') return 707;
  if (preset === 'seams') return 180;
  return 220;
}

function pushVertex(data, x, y, z, u, v) {
  data.positions.push(x, y, z);
  data.normals.push(0, 1, 0);
  data.uvs.push(u, v);
  return (data.positions.length / 3) - 1;
}

function heightAt(x, z) {
  return 0.08 * Math.sin(x * 5.0) * Math.cos(z * 4.0);
}

function generateGrid(preset) {
  const n = presetSize(preset);
  const data = { positions: [], normals: [], uvs: [], indices: [] };
  const seam = preset === 'seams';
  const half = n * 0.5;
  const index = new Uint32Array((n + 1) * (n + 1));

  for (let y = 0; y <= n; y++) {
    for (let x = 0; x <= n; x++) {
      const px = (x / n - 0.5) * 4;
      const pz = (y / n - 0.5) * 4;
      const u = seam && x > half ? 1 - x / n : x / n;
      const v = y / n;
      index[y * (n + 1) + x] = pushVertex(data, px, heightAt(px, pz), pz, u, v);
    }
  }

  for (let y = 0; y < n; y++) {
    for (let x = 0; x < n; x++) {
      const i0 = index[y * (n + 1) + x];
      const i1 = index[y * (n + 1) + x + 1];
      const i2 = index[(y + 1) * (n + 1) + x + 1];
      const i3 = index[(y + 1) * (n + 1) + x];
      data.indices.push(i0, i1, i2, i0, i2, i3);
    }
  }

  computeNormals(data);
  const t0 = performance.now();
  const tangents = computeTangents(data);
  const tangentMs = performance.now() - t0;
  return { ...data, tangents, tangentMs, n };
}

function computeNormals(data) {
  data.normals.fill(0);
  for (let i = 0; i + 2 < data.indices.length; i += 3) {
    const a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
    const ax = data.positions[a * 3], ay = data.positions[a * 3 + 1], az = data.positions[a * 3 + 2];
    const bx = data.positions[b * 3], by = data.positions[b * 3 + 1], bz = data.positions[b * 3 + 2];
    const cx = data.positions[c * 3], cy = data.positions[c * 3 + 1], cz = data.positions[c * 3 + 2];
    const e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    const e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    const nx = e1y * e2z - e1z * e2y;
    const ny = e1z * e2x - e1x * e2z;
    const nz = e1x * e2y - e1y * e2x;
    for (const id of [a, b, c]) {
      data.normals[id * 3] += nx;
      data.normals[id * 3 + 1] += ny;
      data.normals[id * 3 + 2] += nz;
    }
  }
  for (let i = 0; i < data.normals.length; i += 3) {
    const x = data.normals[i], y = data.normals[i + 1], z = data.normals[i + 2];
    const len = Math.hypot(x, y, z) || 1;
    data.normals[i] = x / len;
    data.normals[i + 1] = y / len;
    data.normals[i + 2] = z / len;
  }
}

function computeTangents(data) {
  const vertexCount = data.positions.length / 3;
  const tan = new Float32Array(vertexCount * 3);
  const bit = new Float32Array(vertexCount * 3);
  const out = new Float32Array(vertexCount * 4);
  for (let i = 0; i + 2 < data.indices.length; i += 3) {
    const ids = [data.indices[i], data.indices[i + 1], data.indices[i + 2]];
    const p = ids.map((id) => data.positions.subarray ? null : id);
    void p;
    const i0 = ids[0], i1 = ids[1], i2 = ids[2];
    const p0 = i0 * 3, p1 = i1 * 3, p2 = i2 * 3;
    const u0 = i0 * 2, u1 = i1 * 2, u2 = i2 * 2;
    const e1 = [
      data.positions[p1] - data.positions[p0],
      data.positions[p1 + 1] - data.positions[p0 + 1],
      data.positions[p1 + 2] - data.positions[p0 + 2]
    ];
    const e2 = [
      data.positions[p2] - data.positions[p0],
      data.positions[p2 + 1] - data.positions[p0 + 1],
      data.positions[p2 + 2] - data.positions[p0 + 2]
    ];
    const du1 = data.uvs[u1] - data.uvs[u0];
    const dv1 = data.uvs[u1 + 1] - data.uvs[u0 + 1];
    const du2 = data.uvs[u2] - data.uvs[u0];
    const dv2 = data.uvs[u2 + 1] - data.uvs[u0 + 1];
    const det = du1 * dv2 - du2 * dv1;
    if (Math.abs(det) < 1e-12) continue;
    const r = 1 / det;
    const sdir = e1.map((v, k) => (dv2 * v - dv1 * e2[k]) * r);
    const tdir = e1.map((v, k) => (du1 * e2[k] - du2 * v) * r);
    for (const id of ids) {
      tan[id * 3] += sdir[0];
      tan[id * 3 + 1] += sdir[1];
      tan[id * 3 + 2] += sdir[2];
      bit[id * 3] += tdir[0];
      bit[id * 3 + 1] += tdir[1];
      bit[id * 3 + 2] += tdir[2];
    }
  }
  for (let i = 0; i < vertexCount; i++) {
    const n = [data.normals[i * 3], data.normals[i * 3 + 1], data.normals[i * 3 + 2]];
    const t = [tan[i * 3], tan[i * 3 + 1], tan[i * 3 + 2]];
    const ndt = n[0] * t[0] + n[1] * t[1] + n[2] * t[2];
    let tx = t[0] - n[0] * ndt;
    let ty = t[1] - n[1] * ndt;
    let tz = t[2] - n[2] * ndt;
    const len = Math.hypot(tx, ty, tz) || 1;
    tx /= len; ty /= len; tz /= len;
    const cx = n[1] * tz - n[2] * ty;
    const cy = n[2] * tx - n[0] * tz;
    const cz = n[0] * ty - n[1] * tx;
    const b = [bit[i * 3], bit[i * 3 + 1], bit[i * 3 + 2]];
    out[i * 4] = tx;
    out[i * 4 + 1] = ty;
    out[i * 4 + 2] = tz;
    out[i * 4 + 3] = (cx * b[0] + cy * b[1] + cz * b[2]) < 0 ? -1 : 1;
  }
  return out;
}

function rebuild() {
  const data = generateGrid(state.preset);
  state.stats = {
    vertices: data.positions.length / 3,
    triangles: data.indices.length / 3,
    tangentMs: data.tangentMs,
    memoryMB: data.tangents.byteLength / (1024 * 1024)
  };
  if (state.mesh) {
    state.mesh.geometry.dispose();
    scene.remove(state.mesh);
  }
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(data.positions), 3));
  geometry.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(data.normals), 3));
  geometry.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(data.uvs), 2));
  geometry.setAttribute('tangent', new THREE.BufferAttribute(data.tangents, 4));
  geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(data.indices), 1));
  geometry.computeBoundingSphere();
  state.mesh = new THREE.Mesh(geometry, material);
  scene.add(state.mesh);
  updateStats();
}

function updateStats() {
  material.uniforms.uMode.value = modeCode(state.mode);
  material.uniforms.uNormalScale.value = state.normalScale;
  statsEl.textContent = [
    `triangles: ${state.stats.triangles?.toLocaleString() || '-'}`,
    `vertices:  ${state.stats.vertices?.toLocaleString() || '-'}`,
    `tangent:   ${state.stats.tangentMs?.toFixed(2) || '-'} ms`,
    `buffer:    ${state.stats.memoryMB?.toFixed(2) || '-'} MB`,
    `mode:      ${state.mode}`
  ].join('\n');
}

modeEl.addEventListener('change', () => {
  state.mode = modeEl.value;
  updateStats();
});
presetEl.addEventListener('change', () => {
  state.preset = presetEl.value;
});
scaleEl.addEventListener('input', () => {
  state.normalScale = Number(scaleEl.value);
  updateStats();
});
rebuildEl.addEventListener('click', rebuild);

window.addEventListener('resize', () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

rebuild();
renderer.setAnimationLoop(() => {
  controls.update();
  renderer.render(scene, camera);
});
