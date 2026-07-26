import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';

function escapeHTML(v) { return String(v).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;'); }
function $id(i) { return document.getElementById(i); }

// ── Templates ──
const TEMPLATES = [];
// Template: Cube + Sphere
const tCubeSphere = '{"materials":{"red":{"color":[1,0.2,0.2],"roughness":0.3,"metalness":0.1},"blue":{"color":[0.2,0.4,1],"roughness":0.2,"metalness":0.8}},"prims":[{"type":"Xform","name":"root","children":[{"type":"Mesh","shape":"cube","size":[1,1,1],"pos":[-1.2,0.5,0],"material":"red"},{"type":"Mesh","shape":"sphere","size":[0.8,0.8,0.8],"pos":[1.2,0.8,0],"material":"blue"},{"type":"Mesh","shape":"cylinder","size":[0.6,1.2,0.6],"pos":[0,0,1.5],"material":"red","metalness":0.5,"roughness":0.2}]}]}';
// Template: Robot Arm
const tRobotArm = '{"materials":{"base":{"color":[0.42,0.45,0.5],"roughness":0.6},"upper":{"color":[0.22,0.74,0.97],"roughness":0.45},"lower":{"color":[0.37,0.92,0.83],"roughness":0.5},"gripper":{"color":[0.96,0.62,0.04],"roughness":0.5},"joint":{"color":[0.9,0.9,0.9],"roughness":0.4}},"prims":[{"type":"Xform","children":[{"type":"Mesh","shape":"box","size":[0.62,0.62,0.28],"pos":[0,0.14,0],"material":"base"},{"type":"Xform","pos":[0,0.52,0],"children":[{"type":"Mesh","shape":"cylinder","size":[0.18,0.07,0.18],"pos":[0,0,0],"material":"joint","rot":[1.57,0,0]},{"type":"Xform","pos":[0.45,0,0],"children":[{"type":"Mesh","shape":"box","size":[0.9,0.16,0.16],"pos":[0,0,0],"material":"upper"},{"type":"Xform","pos":[0.45,0,0],"children":[{"type":"Mesh","shape":"cylinder","size":[0.15,0.07,0.15],"pos":[0,0,0],"material":"joint","rot":[1.57,0,0]},{"type":"Xform","pos":[0.35,0,0],"children":[{"type":"Mesh","shape":"box","size":[0.7,0.12,0.12],"pos":[0,0,0],"material":"lower"},{"type":"Mesh","shape":"box","size":[0.13,0.28,0.1],"pos":[0.42,0,0],"material":"gripper"}]}]}]}]}]}]}';
// Template: Solar System
const tSolarSystem = '{"materials":{"sun":{"color":[1,0.8,0.2],"emissive":[1,0.6,0.05],"emissiveIntensity":2,"roughness":0.5},"planet":{"color":[0.3,0.6,1],"roughness":0.3,"metalness":0.2},"moon":{"color":[0.7,0.7,0.7],"roughness":0.8}},"prims":[{"type":"Xform","children":[{"type":"Mesh","shape":"sphere","size":[0.8,0.8,0.8],"pos":[0,0,0],"material":"sun"},{"type":"Xform","pos":[3,0,0],"children":[{"type":"Mesh","shape":"sphere","size":[0.4,0.4,0.4],"pos":[0,0,0],"material":"planet"},{"type":"Xform","pos":[0.7,0,0],"children":[{"type":"Mesh","shape":"sphere","size":[0.12,0.12,0.12],"pos":[0,0,0],"material":"moon"}]}]},{"type":"Xform","pos":[-2.2,0.5,0],"children":[{"type":"Mesh","shape":"sphere","size":[0.25,0.25,0.25],"pos":[0,0,0],"material":"planet","roughness":0.6,"color":[0.9,0.5,0.2]}]}]}]}';
// Template: Primitives Gallery
const tPrimitives = '{"materials":{"a":{"color":[1,0.3,0.3]},"b":{"color":[0.3,1,0.3]},"c":{"color":[0.3,0.3,1]},"d":{"color":[1,1,0.3]},"e":{"color":[1,0.3,1]},"f":{"color":[0.3,1,1]}},"prims":[{"type":"Xform","children":[{"type":"Mesh","shape":"cube","size":[0.6,0.6,0.6],"pos":[-2.5,0.3,0],"material":"a"},{"type":"Mesh","shape":"sphere","size":[0.5,0.5,0.5],"pos":[-1.25,0.25,0],"material":"b"},{"type":"Mesh","shape":"cylinder","size":[0.4,0.8,0.4],"pos":[0,0.4,0],"material":"c"},{"type":"Mesh","shape":"cone","size":[0.5,0.7,0.5],"pos":[1.25,0.35,0],"material":"d"},{"type":"Mesh","shape":"torus","size":[0.5,0.15,0.5],"pos":[2.5,0.5,0],"material":"e","rot":[1.57,0,0]},{"type":"Mesh","shape":"capsule","size":[0.3,0.8,0.3],"pos":[-1.25,0.4,1.5],"material":"f"}]}]}';

TEMPLATES.push(['Cube + Sphere', JSON.parse(tCubeSphere)]);
TEMPLATES.push(['Robot Arm', JSON.parse(tRobotArm)]);
TEMPLATES.push(['Solar System', JSON.parse(tSolarSystem)]);
TEMPLATES.push(['Primitives Gallery', JSON.parse(tPrimitives)]);
// Convert to object
const TEMPLATES_MAP = Object.fromEntries(TEMPLATES);

// ── Shell ──
document.getElementById('demo-root').innerHTML = `
<div class="demo-shell">
  <header class="demo-toolbar">
    <div>
      <a class="demo-back" href="./">Demos</a>
      <h1>Procedural USD Builder</h1>
      <p>Build USD scenes from JSON. Edit the spec, then click Build or press Ctrl+Enter.</p>
    </div>
  </header>
  <main class="builder-main">
    <div class="editor-panel">
      <div class="editor-toolbar">
        <span style="color:var(--muted);font-size:.78rem;font-weight:600">Templates:</span>
        <select id="template-select">
          ${Object.keys(TEMPLATES_MAP).map((t) => `<option value="${escapeHTML(t)}">${escapeHTML(t)}</option>`).join('')}
        </select>
        <button id="load-template-btn">Load</button>
        <span style="flex:1"></span>
        <button id="build-btn" class="primary">Build</button>
        <button id="fit-btn">Fit</button>
      </div>
      <textarea id="scene-json" spellcheck="false"></textarea>
      <div class="editor-status" id="editor-status">Ready. Edit JSON and click Build.</div>
    </div>
    <section class="assets-viewport-wrap">
      <div class="assets-canvas-wrap">
        <canvas id="builder-canvas"></canvas>
        <div class="assets-viewer-overlay" id="viewer-overlay" style="display:none">
          <div class="assets-viewer-placeholder">
            <p>Build a scene from the JSON editor.</p>
          </div>
        </div>
      </div>
      <div class="assets-stats" id="builder-stats">
        <div class="assets-stats-inner">
          <div class="asset-stat"><span class="stat-label">Meshes</span><span class="stat-value" id="s-meshes">0</span></div>
          <div class="asset-stat"><span class="stat-label">Materials</span><span class="stat-value" id="s-materials">0</span></div>
          <div class="asset-stat"><span class="stat-label">Triangles</span><span class="stat-value" id="s-tris">0</span></div>
          <div class="asset-stat"><span class="stat-label">FPS</span><span class="stat-value" id="s-fps">—</span></div>
        </div>
      </div>
    </section>
  </main>
</div>`;

// ── Shape library ──
function createShape(shape, size) {
  const [x, y, z] = size || [1, 1, 1];
  switch (shape) {
    case 'cube': return new THREE.BoxGeometry(x, y, z);
    case 'box': return new THREE.BoxGeometry(x, y, z);
    case 'sphere': return new THREE.SphereGeometry(x * 0.5, 32, 32);
    case 'cylinder': return new THREE.CylinderGeometry(x * 0.5, x * 0.5, y, 32);
    case 'cone': return new THREE.ConeGeometry(x * 0.5, y, 32);
    case 'torus': return new THREE.TorusGeometry(x * 0.5, (z || 0.15), 24, 48);
    case 'capsule': return new THREE.CapsuleGeometry(x * 0.5, y - x, 16, 32);
    default: return new THREE.BoxGeometry(0.5, 0.5, 0.5);
  }
}

// ── Three.js ──
const canvas = $id('builder-canvas');
const renderer = new THREE.WebGLRenderer({ antialias: true, canvas });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0e0e10);

const camera = new THREE.PerspectiveCamera(45, 1, 0.01, 200);
camera.position.set(4, 3, 5);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;

const world = new THREE.Group();
scene.add(world);

scene.add(new THREE.HemisphereLight(0xdde8f6, 0x24272c, 0.8));
const keyLight = new THREE.DirectionalLight(0xffffff, 1.5);
keyLight.position.set(4, 6, 5);
scene.add(keyLight);
const fillLight = new THREE.DirectionalLight(0x9fb7ff, 0.4);
fillLight.position.set(-3, 2, -4);
scene.add(fillLight);

const grid = new THREE.GridHelper(10, 20, 0x44444a, 0x26262b);
grid.position.y = -0.01;
scene.add(grid);

// ── JSON builder ──

function buildSceneFromJSON(jsonText) {
  let spec;
  try {
    spec = JSON.parse(jsonText);
  } catch (e) {
    $id('editor-status').textContent = 'JSON Error: ' + e.message;
    $id('editor-status').classList.add('error-text');
    return;
  }
  $id('editor-status').classList.remove('error-text');

  world.clear();
  let meshCount = 0;
  let triCount = 0;

  const materials = spec.materials || {};
  const materialCache = {};

  function getMaterial(name) {
    if (materialCache[name]) return materialCache[name];
    const def = materials[name] || {};
    const mat = new THREE.MeshPhysicalMaterial({
      color: new THREE.Color(def.color?.[0] ?? 0.8, def.color?.[1] ?? 0.8, def.color?.[2] ?? 0.8),
      metalness: def.metalness ?? 0,
      roughness: def.roughness ?? 0.5,
      emissive: def.emissive ? new THREE.Color(def.emissive[0], def.emissive[1], def.emissive[2]) : new THREE.Color(0, 0, 0),
      emissiveIntensity: def.emissiveIntensity ?? 1,
      envMapIntensity: 0.6,
    });
    materialCache[name] = mat;
    return mat;
  }

  function buildPrim(prim, parent) {
    if (!prim) return;
    const group = new THREE.Group();
    group.name = prim.name || prim.shape || 'prim';

    if (prim.pos) group.position.fromArray(prim.pos);
    if (prim.rot) {
      if (Array.isArray(prim.rot)) {
        if (prim.rot.length === 3) group.rotation.set(prim.rot[0], prim.rot[1], prim.rot[2]);
        else if (prim.rot.length === 4) group.quaternion.fromArray(prim.rot);
      }
    }
    if (prim.scale) group.scale.fromArray(prim.scale);

    if (prim.type === 'Mesh' || prim.shape) {
      const geo = createShape(prim.shape || 'cube', prim.size || [1, 1, 1]);
      const mat = prim.material ? getMaterial(prim.material) : new THREE.MeshPhysicalMaterial({ color: 0x888899, roughness: 0.5 });
      const mesh = new THREE.Mesh(geo, mat);
      mesh.castShadow = true;
      mesh.receiveShadow = true;
      group.add(mesh);
      meshCount++;
      triCount += geo.index ? geo.index.count / 3 : geo.attributes.position.count / 3;
    }

    if (prim.children) {
      for (const child of prim.children) buildPrim(child, group);
    }

    parent.add(group);
    return group;
  }

  if (spec.prims) {
    for (const p of spec.prims) buildPrim(p, world);
  }

  // Stats
  $id('s-meshes').textContent = String(meshCount);
  const matNames = spec.materials ? Object.keys(spec.materials).length : 0;
  $id('s-materials').textContent = String(matNames);
  $id('s-tris').textContent = Math.round(triCount).toLocaleString();
  $id('editor-status').textContent = `Built: ${meshCount} meshes, ${Math.round(triCount).toLocaleString()} triangles.`;

  fitCamera();
}

function fitCamera() {
  const box = new THREE.Box3().setFromObject(world);
  if (box.isEmpty()) return;
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 0.1);
  const dist = maxDim * 2.8;
  controls.target.copy(center);
  camera.position.copy(center).add(new THREE.Vector3(1, 0.6, 1).normalize().multiplyScalar(dist));
  camera.near = Math.max(0.001, dist / 100);
  camera.far = dist * 100;
  camera.updateProjectionMatrix();
  controls.update();
}

// ── Load template ──

function loadTemplate(name) {
  const t = TEMPLATES_MAP[name];
  if (!t) return;
  $id('scene-json').value = JSON.stringify(t, null, 2);
  buildSceneFromJSON($id('scene-json').value);
}

// ── UI ──

$id('load-template-btn').addEventListener('click', () => {
  loadTemplate($id('template-select').value);
});
$id('build-btn').addEventListener('click', () => {
  buildSceneFromJSON($id('scene-json').value);
});
$id('fit-btn').addEventListener('click', fitCamera);

// Keyboard shortcut
$id('scene-json').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    buildSceneFromJSON($id('scene-json').value);
  }
});

// Auto-fit on resize
function onResize() {
  const parent = canvas.parentElement;
  const w = parent.clientWidth;
  const h = parent.clientHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / Math.max(1, h);
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', onResize);
new ResizeObserver(onResize).observe(canvas.parentElement);

// ── Animation loop ──
let fpsState = { frames: 0, last: performance.now() };

function anim(now) {
  requestAnimationFrame(anim);
  fpsState.frames++;
  if (now - fpsState.last >= 500) {
    const fps = Math.round((fpsState.frames * 1000) / (now - fpsState.last));
    $id('s-fps').textContent = String(fps);
    fpsState.frames = 0;
    fpsState.last = now;
  }
  controls.update();
  renderer.render(scene, camera);
}

// ── Main ──

const firstTemplate = Object.keys(TEMPLATES_MAP)[0];
loadTemplate(firstTemplate);
onResize();
requestAnimationFrame(anim);
import { Report } from "../app-report.js";
