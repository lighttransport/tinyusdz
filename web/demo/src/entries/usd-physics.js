import * as THREE from 'three';
import { showLoader, hideLoader } from "../tusd-loader.js";
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';

import { LightUSDLoader } from 'lightusd/LightUSDLoader.js';
import { LightUSDLoaderUtils } from 'lightusd/LightUSDLoaderUtils.js';
import loadMuJoCo from '@lighttransport/mujoco-wasm';

const USDA = './assets/physics-robot-arm.usda';

// ── Shell ──
const root = document.getElementById('demo-root');
root.innerHTML = `
<div class="demo-shell">
  <header class="demo-toolbar">
    <div>
      <a class="demo-back" href="./">Demos</a>
      <h1>USD Physics + MuJoCo</h1>
      <p>Robotic arm simulated with MuJoCo WASM physics. Joint targets are
        tracked via PD servos in real time. <span class="hint">USDZ wireframe
        overlay shows the original USD rest pose.</span></p>
    </div>
    <div class="demo-actions">
      <button id="play-pause" type="button">Pause</button>
      <button id="reset-btn" type="button">Reset</button>
      <button id="fit-btn" type="button">Fit</button>
      <select id="drive-mode">
        <option value="servo">Servo</option>
        <option value="passive">Passive</option>
      </select>
    </div>
  </header>
  <main class="demo-main">
    <section class="viewport-wrap">
      <div id="viewport" class="viewport"></div>
      <div id="status" class="status">Initializing...</div>
    </section>
    <aside class="info-panel">
      <h2>Joints</h2>
      <div id="joint-controls"></div>
      <h2>Stats</h2>
      <dl id="sim-stats">
        <dt>Time</dt><dd id="sim-time">0.000 s</dd>
        <dt>Model</dt><dd id="model-stats">-</dd>
        <dt>USD</dt><dd id="usd-stats">-</dd>
      </dl>
      <h2>Notes</h2>
      <div id="notes">
        <p>Use the sliders to set joint targets. MuJoCo runs a PD servo
          controller at 200 Hz (5 ms timestep). Switch to <em>Passive</em>
          mode to let the arm fall freely under gravity.</p>
        <p><strong>Legend:</strong>
          <span style="color:#38bdf8">●</span> simulation &nbsp;
          <span style="color:#f59e0b;opacity:0.5">●</span> USD rest pose</p>
      </div>
      <details>
        <summary>USD Physics JSON</summary>
        <pre id="physics-json" style="max-height:260px;overflow:auto;margin-top:6px;padding:8px;background:#101013;border:1px solid var(--line);border-radius:4px;color:var(--ok);font-size:0.78rem;line-height:1.45;white-space:pre-wrap;word-break:break-word">{}</pre>
      </details>
      <details>
        <summary>USDA Source</summary>
        <pre id="usda-source" style="max-height:260px;overflow:auto;margin-top:6px;padding:8px;background:#101013;border:1px solid var(--line);border-radius:4px;color:var(--muted);font-size:0.74rem;line-height:1.35;white-space:pre-wrap;word-break:break-word"></pre>
      </details>
    </aside>
  </main>
</div>`;

// ── DOM refs ──
const $ = (id) => document.getElementById(id);
const viewport = $('viewport');
const statusEl = $('status');
const jointControls = $('joint-controls');
const simTime = $('sim-time');
const modelStats = $('model-stats');
const usdStats = $('usd-stats');
const physicsJson = $('physics-json');
const usdaSource = $('usda-source');

function setStatus(s) { statusEl.textContent = s; }

// ── MuJoCo model configuration extracted from USD physics data ──

const JOINT_CONFIG = {
  ShoulderJoint: {
    label: 'Shoulder',
    axis: [0, 1, 0],
    range: [-130, 130],
    damping: 5, kp: 58, kd: 13, default: 34,
  },
  ElbowJoint: {
    label: 'Elbow',
    axis: [0, 1, 0],
    range: [-135, 135],
    damping: 3, kp: 72, kd: 11, default: -52,
  },
};

function deg(r) { return THREE.MathUtils.radToDeg(r); }
function rad(d) { return THREE.MathUtils.degToRad(d); }

// ── Three.js setup ──

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x0e0e10);

const camera = new THREE.PerspectiveCamera(46, 1, 0.01, 100);
camera.up.set(0, 0, 1);
camera.position.set(2.6, -3.0, 2.1);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
viewport.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0.65, 0, 0.55);
controls.enableDamping = true;
controls.dampingFactor = 0.08;

const world = new THREE.Group();
const restRoot = new THREE.Group();
const simRoot = new THREE.Group();
scene.add(world);
world.add(restRoot);
world.add(simRoot);

scene.add(new THREE.HemisphereLight(0xdde8f6, 0x24272c, 1.5));
const keyLight = new THREE.DirectionalLight(0xffffff, 2.3);
keyLight.position.set(2.6, -3.6, 4.2);
keyLight.castShadow = true;
keyLight.shadow.mapSize.set(1024, 1024);
scene.add(keyLight);
const fillLight = new THREE.DirectionalLight(0x9fb7ff, 0.5);
fillLight.position.set(-3, 2, -2);
scene.add(fillLight);

const grid = new THREE.GridHelper(8, 32, 0x4a5660, 0x262b30);
grid.rotation.x = Math.PI / 2;
scene.add(grid);
scene.add(new THREE.AxesHelper(0.35));

// ── Materials ──

const matBase = new THREE.MeshStandardMaterial({ color: 0x6b7280, roughness: 0.6, metalness: 0.05 });
const matUpper = new THREE.MeshStandardMaterial({ color: 0x38bdf8, roughness: 0.45, metalness: 0.05 });
const matLower = new THREE.MeshStandardMaterial({ color: 0x5eead4, roughness: 0.5, metalness: 0.03 });
const matJoint = new THREE.MeshStandardMaterial({ color: 0xe5e7eb, roughness: 0.4, metalness: 0.05 });
const matGripper = new THREE.MeshStandardMaterial({ color: 0xf59e0b, roughness: 0.5, metalness: 0.03 });

function addBox(parent, size, pos, mat) {
  const g = new THREE.BoxGeometry(size[0], size[1], size[2]);
  const m = new THREE.Mesh(g, mat);
  m.position.fromArray(pos);
  m.castShadow = true;
  m.receiveShadow = true;
  parent.add(m);
  return m;
}

function jointMarker(parent) {
  const g = new THREE.CylinderGeometry(0.09, 0.09, 0.07, 32);
  const m = new THREE.Mesh(g, matJoint);
  m.rotation.x = Math.PI / 2;
  m.castShadow = true;
  parent.add(m);
  return m;
}

// ── Simulation display (programmatic arm matching USD physics) ──

function buildSimDisplay() {
  addBox(simRoot, [0.62, 0.62, 0.28], [0, 0, 0.14], matBase);
  const shoulder = new THREE.Group();
  shoulder.name = 'sim_shoulder';
  shoulder.position.set(0, 0, 0.52);
  simRoot.add(shoulder);
  jointMarker(shoulder);
  addBox(shoulder, [0.9, 0.16, 0.16], [0.45, 0, 0], matUpper);
  const elbow = new THREE.Group();
  elbow.name = 'sim_elbow';
  elbow.position.set(0.9, 0, 0);
  shoulder.add(elbow);
  jointMarker(elbow);
  addBox(elbow, [0.7, 0.12, 0.12], [0.35, 0, 0], matLower);
  addBox(elbow, [0.13, 0.28, 0.1], [0.77, 0, 0], matGripper);
  return { shoulder, el: elbow };
}

const simParts = buildSimDisplay();

// ── USD rest-pose display ──

function styleRestPose(root) {
  root.traverse((obj) => {
    if (obj.isMesh && obj.material) {
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      obj.material = mats.map((mat) => {
        const c = mat.clone();
        c.color.set(0xf59e0b);
        c.transparent = true;
        c.opacity = 0.2;
        c.depthWrite = false;
        c.wireframe = true;
        return c;
      });
    }
  });
}

function dispose(root) {
  root.traverse((obj) => {
    obj.geometry?.dispose?.();
    if (obj.material) {
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      for (const m of mats) m.dispose?.();
    }
  });
}

// ── MuJoCo state ──

const mjState = {
  module: null,
  model: null,
  data: null,
  paused: false,
  driveMode: 'servo',
  speed: 1,
  targets: { shoulder: rad(34), elbow: rad(-52) },
};

// ── Fit ──

function fit() {
  const box = new THREE.Box3().setFromObject(simRoot);
  if (box.isEmpty()) return;
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3());
  const maxDim = Math.max(size.x, size.y, size.z, 0.1);
  const dist = maxDim * 2.2;
  controls.target.copy(center);
  camera.position.copy(center).add(new THREE.Vector3(1.7, -2.2, 1.35).normalize().multiplyScalar(dist));
  camera.near = Math.max(0.001, dist / 100);
  camera.far = dist * 100;
  camera.updateProjectionMatrix();
  controls.update();
}

// ── MuJoCo model ──

function buildMuJoCoModel(mj) {
  const spec = new mj.MjSpec();
  spec.setModelName('USDPhysicsArm');
  spec.setTimestep(0.005);
  spec.setGravity(0, 0, -9.80665);
  const wb = spec.worldBody();

  // ground
  const plane = mj.MjsGeom.add(wb, 'ground');
  mj.MjsGeom.setType(plane, mj.GEOM_PLANE);
  mj.MjsGeom.setSize(plane, 5, 5, 0.02);
  mj.MjsGeom.setRGBA(plane, 0.16, 0.18, 0.2, 1);
  mj.MjsGeom.setFriction(plane, 1.0, 0.01, 0.001);

  // light
  const light = mj.MjsLight.add(wb, 'key');
  mj.MjsLight.setPos(light, 2.5, -3.0, 4.0);
  mj.MjsLight.setDir(light, -0.5, 0.65, -1.0);
  mj.MjsLight.setDiffuse(light, 0.8, 0.8, 0.8);

  // base (static)
  const base = mj.MjsBody.add(wb, 'Base');
  mj.MjsBody.setPos(base, 0, 0, 0);
  const baseGeom = mj.MjsGeom.add(base, 'BaseBlock');
  mj.MjsGeom.setType(baseGeom, mj.GEOM_BOX);
  mj.MjsGeom.setSize(baseGeom, 0.31, 0.31, 0.14);
  mj.MjsGeom.setPos(baseGeom, 0, 0, 0.14);
  mj.MjsGeom.setRGBA(baseGeom, 0.42, 0.45, 0.5, 1);
  mj.MjsGeom.setMass(baseGeom, 8);

  // shoulder
  const shoulder = mj.MjsBody.add(base, 'Shoulder');
  mj.MjsBody.setPos(shoulder, 0, 0, 0.52);
  const sj = mj.MjsJoint.add(shoulder, 'ShoulderJoint');
  mj.MjsJoint.setType(sj, mj.JNT_HINGE);
  mj.MjsJoint.setAxis(sj, 0, 1, 0);
  mj.MjsJoint.setRange(sj, rad(-130), rad(130));
  mj.MjsJoint.setDamping(sj, 5.0);
  const upperGeom = mj.MjsGeom.add(shoulder, 'UpperArm');
  mj.MjsGeom.setType(upperGeom, mj.GEOM_BOX);
  mj.MjsGeom.setSize(upperGeom, 0.45, 0.08, 0.08);
  mj.MjsGeom.setPos(upperGeom, 0.45, 0, 0);
  mj.MjsGeom.setRGBA(upperGeom, 0.22, 0.74, 0.97, 1);
  mj.MjsGeom.setMass(upperGeom, 2.2);

  // elbow
  const elbow = mj.MjsBody.add(shoulder, 'Elbow');
  mj.MjsBody.setPos(elbow, 0.9, 0, 0);
  const ej = mj.MjsJoint.add(elbow, 'ElbowJoint');
  mj.MjsJoint.setType(ej, mj.JNT_HINGE);
  mj.MjsJoint.setAxis(ej, 0, 1, 0);
  mj.MjsJoint.setRange(ej, rad(-135), rad(135));
  mj.MjsJoint.setDamping(ej, 3.0);
  const foreGeom = mj.MjsGeom.add(elbow, 'Forearm');
  mj.MjsGeom.setType(foreGeom, mj.GEOM_BOX);
  mj.MjsGeom.setSize(foreGeom, 0.35, 0.06, 0.06);
  mj.MjsGeom.setPos(foreGeom, 0.35, 0, 0);
  mj.MjsGeom.setRGBA(foreGeom, 0.37, 0.92, 0.83, 1);
  mj.MjsGeom.setMass(foreGeom, 1.4);
  const gripGeom = mj.MjsGeom.add(elbow, 'Gripper');
  mj.MjsGeom.setType(gripGeom, mj.GEOM_BOX);
  mj.MjsGeom.setSize(gripGeom, 0.065, 0.14, 0.05);
  mj.MjsGeom.setPos(gripGeom, 0.77, 0, 0);
  mj.MjsGeom.setRGBA(gripGeom, 0.96, 0.62, 0.04, 1);
  mj.MjsGeom.setMass(gripGeom, 0.25);

  const model = spec.compile();
  mjState.model = model;
  mjState.data = new mj.PhysicsData(model);
  spec.delete();
  resetMuJoCo();
  modelStats.textContent = `${model.nbody()} bodies, ${model.njnt()} joints, ${model.nq()} qpos`;
}

function resetMuJoCo() {
  const mj = mjState.module;
  if (!mj || !mjState.model || !mjState.data) return;
  mj.mj_resetData(mjState.model, mjState.data);
  const qpos = mjState.data.qpos();
  qpos[0] = mjState.targets.shoulder;
  qpos[1] = mjState.targets.elbow;
  mjState.data.qvel().fill(0);
  mj.mj_forward(mjState.model, mjState.data);
  updateDisplay();
}

function applyServo(dt) {
  if (mjState.driveMode !== 'servo') return;
  const qpos = mjState.data.qpos();
  const qvel = mjState.data.qvel();
  const cfg = [JOINT_CONFIG.ShoulderJoint, JOINT_CONFIG.ElbowJoint];
  const targets = [mjState.targets.shoulder, mjState.targets.elbow];
  for (let i = 0; i < 2; i++) {
    const err = targets[i] - qpos[i];
    qvel[i] += (err * cfg[i].kp - qvel[i] * cfg[i].kd) * dt;
  }
}

function stepPhysics(dt) {
  const mj = mjState.module;
  if (!mjState.model || !mjState.data || mjState.paused) return;
  const baseDt = mjState.model.timestep();
  const scaled = Math.min(0.05, dt * mjState.speed);
  const steps = Math.max(1, Math.min(12, Math.ceil(scaled / baseDt)));
  for (let i = 0; i < steps; i++) {
    applyServo(baseDt);
    mj.mj_step(mjState.model, mjState.data);
  }
  updateDisplay();
}

function updateDisplay() {
  if (!mjState.data || !simParts) return;
  const qpos = mjState.data.qpos();
  simParts.shoulder.rotation.y = qpos[0] || 0;
  simParts.el.rotation.y = qpos[1] || 0;
  simTime.textContent = `${mjState.data.time().toFixed(3)} s`;
}

// ── Joint controls ──

function buildJointControls() {
  for (const [key, cfg] of Object.entries(JOINT_CONFIG)) {
    const min = cfg.range[0];
    const max = cfg.range[1];
    const row = document.createElement('div');
    row.style.cssText = 'display:flex;align-items:center;gap:8px;margin-bottom:8px';

    const label = document.createElement('label');
    label.textContent = cfg.label;
    label.style.cssText = 'flex:0 0 64px;font-size:0.85rem;color:var(--text)';

    const number = document.createElement('input');
    number.type = 'number';
    number.min = min; number.max = max; number.step = 1;
    number.value = cfg.default;
    number.style.cssText = 'width:56px;padding:3px 5px;border:1px solid var(--line-strong);border-radius:4px;background:var(--panel-2);color:var(--text);font-size:0.82rem';

    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = min; slider.max = max; slider.step = 1;
    slider.value = cfg.default;
    slider.style.cssText = 'flex:1;min-width:0';

    const targetKey = key.charAt(0).toLowerCase() + key.slice(1).replace('Joint', '');
    row.appendChild(label);
    row.appendChild(number);
    row.appendChild(slider);

    const sync = (v) => {
      const num = Number(v);
      slider.value = String(num);
      number.value = String(num);
      mjState.targets[targetKey.startsWith('shoulder') ? 'shoulder' : 'elbow'] = rad(num);
    };
    const syncDeg = (v) => sync(Math.round(Number(v)));
    slider.addEventListener('input', () => syncDeg(slider.value));
    number.addEventListener('input', () => syncDeg(number.value));
    jointControls.appendChild(row);
  }
}

// ── USD loading ──

async function loadUSDScene(loader) {
  setStatus('Loading USD scene...');
  const resp = await fetch(USDA);
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  const text = await resp.text();
  usdaSource.textContent = text;

  const bytes = new TextEncoder().encode(text);
  const scene = await new Promise((resolve, reject) => {
    loader.parse(bytes.buffer, 'robot-arm.usda', resolve, reject, { backend: 'legacy' });
  });
  const rootNode = scene.getDefaultRootNode();
  const mat = LightUSDLoaderUtils.createDefaultMaterial();
  const usdObj = await LightUSDLoaderUtils.buildThreeNode(rootNode, mat, scene, { overrideMaterial: false });
  usdObj.name = 'USD rest pose';
  styleRestPose(usdObj);
  restRoot.add(usdObj);

  // Extract physics JSON
  const native = new loader.native_.LightUSDLoaderNative();
  const loaded = native.loadAsLayerFromBinary(bytes, 'robot-arm.usda');
  if (loaded) {
    const json = native.extractPhysicsSceneJSON();
    if (json) {
      physicsJson.textContent = json;
      const parsed = JSON.parse(json);
      usdStats.textContent = `${parsed.prims?.length || 0} prims`;
    }
    native.delete();
  }
  usdStats.textContent = usdStats.textContent || '—';
  setStatus('USD scene loaded.');
}

// ── UI wiring ──

$('play-pause').addEventListener('click', () => {
  mjState.paused = !mjState.paused;
  $('play-pause').textContent = mjState.paused ? 'Play' : 'Pause';
});
$('reset-btn').addEventListener('click', resetMuJoCo);
$('fit-btn').addEventListener('click', fit);
$('drive-mode').addEventListener('change', () => {
  mjState.driveMode = $('drive-mode').value;
});

// ── Main ──

async function main() {
  buildJointControls();
  fit();

  setStatus('Initializing LightUSD...');
  const loader = new LightUSDLoader(null, { maxMemoryLimitMB: 256 });
  showLoader("Loading LightUSD WASM...", document.getElementById("viewport"));
  try {
    await loader.init({ useZstdCompressedWasm: false, useMemory64: false, backend: 'legacy' });
  } finally {
    hideLoader();
  }
  LightUSDLoaderUtils.setLightUSD(loader.native_);

  await loadUSDScene(loader);

  setStatus('Loading MuJoCo WASM...');
  const mj = await loadMuJoCo();
  mjState.module = mj;
  buildMuJoCoModel(mj);

  fit();
  setStatus('Simulating. Drag to orbit, scroll to zoom.');

  let lastTime = performance.now();
  function anim(now) {
    requestAnimationFrame(anim);
    const dt = Math.min(0.05, (now - lastTime) / 1000);
    lastTime = now;
    stepPhysics(dt);
    controls.update();
    renderer.render(scene, camera);
  }
  requestAnimationFrame(anim);
}

function onResize() {
  const rect = viewport.getBoundingClientRect();
  camera.aspect = Math.max(1, rect.width) / Math.max(1, rect.height);
  camera.updateProjectionMatrix();
  renderer.setSize(Math.max(1, rect.width), Math.max(1, rect.height), false);
}
window.addEventListener('resize', onResize);

main().catch((err) => {
  console.error(err);
  setStatus(`Error: ${err.message}`);
});
