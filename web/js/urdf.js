import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { OBJLoader } from 'three/examples/jsm/loaders/OBJLoader.js';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import GUI from 'three/examples/jsm/libs/lil-gui.module.min.js';
import URDFLoader from 'urdf-loader';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import {
  createConfiguredTinyUSDZLoader,
  parseUSDSceneFromArrayBuffer
} from 'tinyusdz/LoaderConfigUtils.js';

const state = {
  robot: null,
  usdObject: null,
  usdRestObject: null,
  usdArticulation: null,
  sourceGhost: null,
  usdGhost: null,
  usdLinkBindings: [],
  sourceRestLinkMatrices: new Map(),
  inputText: '',
  inputName: '',
  inputFormat: '',
  usdName: '',
  latestUSDBytes: null,
  latestUSDFormat: '',
  exportPayload: null,
  assetFiles: new Map(),
  objectUrls: new Map(),
  meshCache: new Map(),
  nativeMeshBuffers: new Map(),
  tinyLoader: null,
  nativeExporter: null,
  joints: {},
  jointValues: {},
  jointControls: new Map(),
  collisionMeshes: [],
  visualMeshes: [],
  settings: {
    upAxis: 'Z',
    showVisuals: true,
    showCollisions: false,
    ignoreJointLimits: false,
    hideFixedJoints: true,
    animateJoints: false,
    animationSpeed: 0.8,
    autocenter: true,
    packageRoot: '',
    ghostUSDInSource: false,
    ghostSourceInUSD: false,
    ghostOpacity: 0.28,
    showAxisHelper: true,
    showLinkLines: false,
    showJointArrows: false,
    showLinkNames: false,
    showJointNames: false,
    jointsCollapsed: false,
    applyHomePose: false
  }
};

const USD_MESH_EXTENSIONS = new Set(['.usd', '.usda', '.usdc', '.usdz']);

function createViewScene() {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x202124);
  const root = new THREE.Group();
  const ghostRoot = new THREE.Group();
  const debugRoot = new THREE.Group();
  const axisHelper = new THREE.AxesHelper(0.35);
  axisHelper.name = 'WorldAxesHelper';
  axisHelper.renderOrder = 30;
  axisHelper.visible = state.settings.showAxisHelper;
  scene.add(root);
  scene.add(ghostRoot);
  scene.add(debugRoot);
  scene.add(axisHelper);
  scene.add(new THREE.HemisphereLight(0xddeeff, 0x303030, 1.6));
  const dirLight = new THREE.DirectionalLight(0xffffff, 2.4);
  dirLight.position.set(4, 6, 3);
  dirLight.castShadow = true;
  scene.add(dirLight);
  scene.add(new THREE.GridHelper(20, 40, 0x586069, 0x33383d));
  return { scene, root, ghostRoot, debugRoot, axisHelper };
}

const sourceView = createViewScene();
const usdView = createViewScene();

const camera = new THREE.PerspectiveCamera(50, window.innerWidth / window.innerHeight, 0.01, 2000);
camera.position.set(4, 3, 6);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0.6, 0);
controls.update();

const robotGroup = sourceView.root;
const usdGroup = usdView.root;

const collisionMaterial = new THREE.MeshStandardMaterial({
  color: 0xffd23f,
  emissive: 0x332300,
  roughness: 0.58,
  metalness: 0.0,
  transparent: true,
  opacity: 0.42,
  depthWrite: false,
  polygonOffset: true,
  polygonOffsetFactor: -1,
  polygonOffsetUnits: -1
});

const sourceLinkLineMaterial = new THREE.LineBasicMaterial({ color: 0x4fc3ff, transparent: true, opacity: 0.9 });
const usdLinkLineMaterial = new THREE.LineBasicMaterial({ color: 0xffb24d, transparent: true, opacity: 0.9 });
const sourceJointMaterial = new THREE.MeshStandardMaterial({
  color: 0x26aee6,
  emissive: 0x062536,
  roughness: 0.54,
  metalness: 0.02,
  depthTest: false
});
const usdJointMaterial = new THREE.MeshStandardMaterial({
  color: 0xffb13b,
  emissive: 0x3a1f02,
  roughness: 0.54,
  metalness: 0.02,
  depthTest: false
});
const jointArrowShaftGeometry = new THREE.CylinderGeometry(0.012, 0.012, 0.078, 24, 1);
const jointArrowHeadGeometry = new THREE.ConeGeometry(0.027, 0.04, 24, 1);
const sourceLabelColor = '#8be9ff';
const usdLabelColor = '#ffd08a';

const statusEl = document.getElementById('status');
const panelEl = document.getElementById('panel');
const jointHeaderEl = document.getElementById('jointHeader');
const jointFoldIconEl = document.getElementById('jointFoldIcon');
const jointControlsEl = document.getElementById('jointControls');
const sourceLabelEl = document.getElementById('sourceLabel');
const usdLabelEl = document.getElementById('usdLabel');
const sourceExportButton = document.getElementById('exportSource');
const convertToUSDButton = document.getElementById('convertToUSD');
const convertToSourceButton = document.getElementById('convertToSource');
const fitViewButton = document.getElementById('fitView');
const exportButtons = [
  document.getElementById('exportUSDA'),
  document.getElementById('exportUSDC'),
  document.getElementById('exportUSDZ')
];
const DEFAULT_USDC_EXPORT_LIMIT_MB = 2048;
const DEFAULT_MEM_EXPORT_LIMIT_MB = 4096;

function parsePositiveInt(value) {
  const n = Number(value);
  return Number.isInteger(n) && n > 0 ? n : 0;
}

function resolveUSDCExportCapsFromRuntime() {
  const search = (typeof window !== 'undefined' && window.location?.search) ? window.location.search : '';
  const params = new URLSearchParams(search || '');

  const queryUsdc = parsePositiveInt(params.get('maxUsdcMb'));
  const queryMem = parsePositiveInt(params.get('maxMemMb'));
  const globalUsdc = parsePositiveInt(
    typeof window !== 'undefined' ? window.__TINYUSDZ_MAX_USDC_MB : undefined
  );
  const globalMem = parsePositiveInt(
    typeof window !== 'undefined' ? window.__TINYUSDZ_MAX_MEM_MB : undefined
  );

  return {
    maxUsdcMb: queryUsdc || globalUsdc || DEFAULT_USDC_EXPORT_LIMIT_MB,
    maxMemMb: queryMem || globalMem || DEFAULT_MEM_EXPORT_LIMIT_MB,
  };
}

function setStatus(text) {
  statusEl.textContent = text;
}

function updateButtonStates() {
  const hasSource = Boolean(state.robot);
  const hasUSD = Boolean(state.usdObject || state.latestUSDBytes);
  sourceExportButton.disabled = !hasSource;
  convertToUSDButton.disabled = !hasSource;
  convertToSourceButton.disabled = !hasUSD;
  fitViewButton.disabled = !(hasSource || hasUSD);
  for (const button of exportButtons) button.disabled = !(hasSource || hasUSD);
}

function disposeObject(root) {
  root.traverse((obj) => {
    if (obj.geometry) obj.geometry.dispose();
    if (obj.material && obj.material !== collisionMaterial) {
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      for (const mat of mats) {
        if (mat.map?.userData?.debugLabelTexture) mat.map.dispose();
        mat.dispose?.();
      }
    }
  });
}

function clearObjectFromGroup(group, object) {
  if (!object) return;
  group.remove(object);
  disposeObject(object);
}

function clearGhosts() {
  clearObjectFromGroup(sourceView.ghostRoot, state.usdGhost);
  clearObjectFromGroup(usdView.ghostRoot, state.sourceGhost);
  state.usdGhost = null;
  state.sourceGhost = null;
}

function clearDebugRoot(root) {
  while (root.children.length) {
    const child = root.children.pop();
    disposeObject(child);
  }
}

function clearDebugVisualizations() {
  clearDebugRoot(sourceView.debugRoot);
  clearDebugRoot(usdView.debugRoot);
}

function clearRobot() {
  if (state.robot) {
    clearObjectFromGroup(robotGroup, state.robot);
  }
  state.robot = null;
  state.inputText = '';
  state.inputName = '';
  state.inputFormat = '';
  state.exportPayload = null;
  state.sourceRestLinkMatrices.clear();
  state.usdLinkBindings = [];
  state.nativeMeshBuffers.clear();
  state.joints = {};
  state.jointValues = {};
  state.collisionMeshes = [];
  state.visualMeshes = [];
  jointControlsEl.innerHTML = '';
  updateButtonStates();
  clearGhosts();
  clearDebugVisualizations();
  updateLabels();
}

function clearUSD() {
  clearObjectFromGroup(usdGroup, state.usdObject);
  if (state.usdRestObject && state.usdRestObject !== state.usdObject) {
    disposeObject(state.usdRestObject);
  }
  state.usdObject = null;
  state.usdRestObject = null;
  state.usdArticulation = null;
  state.usdName = '';
  state.latestUSDBytes = null;
  state.latestUSDFormat = '';
  state.usdLinkBindings = [];
  updateButtonStates();
  clearGhosts();
  clearDebugVisualizations();
  updateLabels();
}

function rebuildAssetFiles(files) {
  for (const url of state.objectUrls.values()) URL.revokeObjectURL(url);
  state.assetFiles.clear();
  state.objectUrls.clear();
  state.meshCache.clear();

  for (const file of files) {
    rememberAssetFile(file);
  }
  setStatus(`${files.length} asset files indexed.`);
}

function normalizePath(path) {
  return path.replace(/\\/g, '/').replace(/^\.?\//, '');
}

function dirname(path) {
  const normalized = normalizePath(path || '');
  const slash = normalized.lastIndexOf('/');
  return slash >= 0 ? normalized.slice(0, slash) : '';
}

function joinPath(base, path) {
  const cleanPath = normalizePath(path || '');
  if (!base) return cleanPath;
  return normalizePath(`${base}/${cleanPath}`);
}

function rememberAssetFile(file) {
  const rel = file.webkitRelativePath || file.name;
  state.assetFiles.set(rel, file);
  state.assetFiles.set(file.name, file);
}

function resolveAssetEntry(assetPath) {
  const raw = normalizePath(assetPath || '');
  const withoutPackage = raw.replace(/^package:\/\//, '');
  const withoutRoot = state.settings.packageRoot
    ? withoutPackage.replace(new RegExp(`^${state.settings.packageRoot.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}/`), '')
    : withoutPackage;
  const candidates = [
    raw,
    withoutPackage,
    withoutRoot,
    withoutPackage.split('/').slice(1).join('/'),
    withoutPackage.split('/').pop()
  ].filter(Boolean);

  for (const candidate of candidates) {
    if (state.assetFiles.has(candidate)) {
      return { rel: candidate, file: state.assetFiles.get(candidate) };
    }
  }
  for (const [rel, file] of state.assetFiles) {
    if (rel.endsWith(withoutPackage) || rel.endsWith(withoutRoot)) return { rel, file };
  }
  return null;
}

function resolveAssetFile(assetPath) {
  return resolveAssetEntry(assetPath)?.file || null;
}

function objectURLForFile(file) {
  if (!state.objectUrls.has(file)) {
    state.objectUrls.set(file, URL.createObjectURL(file));
  }
  return state.objectUrls.get(file);
}

function extension(path) {
  const clean = (path || '').split('?')[0].split('#')[0];
  const dot = clean.lastIndexOf('.');
  return dot >= 0 ? clean.slice(dot).toLowerCase() : '';
}

async function ensureTinyLoader() {
  if (!state.tinyLoader) {
    setStatus('Loading TinyUSDZ WASM...');
    state.tinyLoader = await createConfiguredTinyUSDZLoader();
    TinyUSDZLoaderUtils.setTinyUSDZ(state.tinyLoader.native_);
  }
  return state.tinyLoader;
}

async function loadUSDMeshFromFile(file) {
  const loader = await ensureTinyLoader();
  const sceneData = await parseUSDSceneFromArrayBuffer(loader, await file.arrayBuffer(), file.name);
  const rootNode = sceneData.getDefaultRootNode();
  const defaultMaterial = TinyUSDZLoaderUtils.createDefaultMaterial();
  return TinyUSDZLoaderUtils.buildThreeNode(rootNode, defaultMaterial, sceneData, { overrideMaterial: false });
}

async function loadUSDObjectFromBytes(bytes, filename) {
  const loader = await ensureTinyLoader();
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const sceneData = await parseUSDSceneFromArrayBuffer(
    loader,
    data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
    filename
  );
  const rootNode = sceneData.getDefaultRootNode();
  const defaultMaterial = TinyUSDZLoaderUtils.createDefaultMaterial();
  const object = await TinyUSDZLoaderUtils.buildThreeNode(rootNode, defaultMaterial, sceneData, { overrideMaterial: false });
  object.name = filename.replace(/\.[^.]+$/, '') || 'usd_scene';
  return object;
}

async function extractUSDPhysicsJSONFromBytes(bytes, filename) {
  const native = await ensureNativeExporter();
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  if (!native.loadFromBinary(data, filename || 'scene.usd')) {
    throw new Error(native.error() || 'Failed to load USD for physics extraction.');
  }
  const jsonText = native.extractPhysicsSceneJSON();
  if (!jsonText) throw new Error(native.error() || 'USD Physics extraction failed.');
  return JSON.parse(jsonText);
}

async function loadUSDFile(file) {
  clearUSD();
  const bytes = new Uint8Array(await file.arrayBuffer());
  const object = await loadUSDObjectFromBytes(bytes, file.name);
  state.usdObject = object;
  state.usdRestObject = object;
  state.usdName = file.name;
  state.latestUSDBytes = bytes;
  state.latestUSDFormat = extension(file.name).slice(1) || 'usd';
  usdGroup.add(object);
  applySceneOrientation();
  try {
    const extracted = await extractUSDPhysicsJSONFromBytes(bytes, file.name);
    annotateUSDRenderableClasses(object, extracted);
    const model = usdPhysicsToSourceModel(extracted);
    if (model.links.length && model.joints.length) {
      const articulated = buildArticulatedRobotFromUSD(model, object, {
        name: object.name,
        resetMeshLists: false,
        inferMissingJointFrames: true
      });
      usdGroup.remove(object);
      state.usdObject = articulated;
      state.usdArticulation = articulated;
      usdGroup.add(articulated);
      rebuildJointControls();
      updateRobotInfo({
        name: model.name,
        links: new Map(model.links.map((link) => [link.name, link])),
        joints: model.joints,
        meshes: (extracted.prims || []).filter((prim) => prim.geometry).length
      });
    }
  } catch (err) {
    console.warn('USD Physics extraction skipped:', err);
  }
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
  applyVisibility();
  if (state.settings.autocenter) fitCamera(currentFitObjects());
  setStatus(`Loaded USD ${file.name}`);
}

async function loadOBJMeshFromFile(file) {
  const url = objectURLForFile(file);
  const loader = new OBJLoader();
  return loader.loadAsync(url);
}

async function loadSTLMeshFromFile(file) {
  const geometry = new STLLoader().parse(await file.arrayBuffer());
  geometry.computeVertexNormals();
  const mesh = new THREE.Mesh(
    geometry,
    new THREE.MeshStandardMaterial({ color: 0xb8c0c8, roughness: 0.62, metalness: 0.05 })
  );
  mesh.name = file.name.replace(/\.[^.]+$/, '');
  const group = new THREE.Group();
  group.name = mesh.name;
  group.add(mesh);
  return group;
}

function makeMissingMesh(path) {
  const group = new THREE.Group();
  group.name = `missing_${path || 'mesh'}`;
  const mesh = new THREE.Mesh(
    new THREE.BoxGeometry(0.08, 0.08, 0.08),
    new THREE.MeshStandardMaterial({ color: 0xd84a4a, roughness: 0.7 })
  );
  group.add(mesh);
  return group;
}

async function loadURDFMesh(path) {
  const file = resolveAssetFile(path);
  if (!file) {
    setStatus(`Missing mesh asset: ${path}`);
    return makeMissingMesh(path);
  }

  const ext = extension(file.name || path);
  if (ext === '.obj') return loadOBJMeshFromFile(file);
  if (ext === '.stl') return loadSTLMeshFromFile(file);
  if (USD_MESH_EXTENSIONS.has(ext)) return loadUSDMeshFromFile(file);

  setStatus(`Unsupported mesh extension ${ext || '(none)'} for ${path}`);
  return makeMissingMesh(path);
}

async function loadMeshObject(path) {
  const entry = resolveAssetEntry(path);
  if (!entry) {
    setStatus(`Missing mesh asset: ${path}`);
    return makeMissingMesh(path);
  }

  if (!state.meshCache.has(entry.rel)) {
    const ext = extension(entry.file.name || path);
    let promise = null;
    if (ext === '.obj') {
      promise = loadOBJMeshFromFile(entry.file);
    } else if (ext === '.stl') {
      promise = loadSTLMeshFromFile(entry.file);
    } else if (USD_MESH_EXTENSIONS.has(ext)) {
      promise = loadUSDMeshFromFile(entry.file);
    } else {
      setStatus(`Unsupported mesh extension ${ext || '(none)'} for ${path}`);
      promise = Promise.resolve(makeMissingMesh(path));
    }
    state.meshCache.set(entry.rel, promise);
  }

  return cloneRenderableObject(await state.meshCache.get(entry.rel));
}

function cloneRenderableObject(source) {
  const savedUserData = [];
  source.traverse((obj) => {
    savedUserData.push([obj, obj.userData]);
    obj.userData = {};
  });
  let clone = null;
  try {
    clone = source.clone(true);
  } finally {
    for (const [obj, userData] of savedUserData) obj.userData = userData;
  }
  clone.traverse((obj) => {
    if (!obj.isMesh) return;
    if (obj.geometry) obj.geometry = obj.geometry.clone();
    if (Array.isArray(obj.material)) {
      obj.material = obj.material.map((mat) => mat.clone?.() || mat);
    } else if (obj.material) {
      obj.material = obj.material.clone?.() || obj.material;
    }
  });
  return clone;
}

function makeGhostObject(source, color) {
  const ghost = cloneRenderableObject(source);
  const ghostMaterial = new THREE.MeshStandardMaterial({
    color,
    roughness: 0.8,
    metalness: 0.0,
    transparent: true,
    opacity: state.settings.ghostOpacity,
    depthWrite: false
  });
  ghost.traverse((obj) => {
    if (!obj.isMesh) return;
    obj.material = ghostMaterial.clone();
  });
  return ghost;
}

function setGhostOpacity(root, opacity) {
  root?.traverse((obj) => {
    if (obj.isMesh && obj.material) {
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      for (const mat of mats) {
        mat.opacity = opacity;
        mat.transparent = true;
        mat.depthWrite = false;
      }
    }
  });
}

function syncObjectTransforms(source, target) {
  if (!source || !target) return;
  const sourceObjects = [];
  const targetObjects = [];
  source.traverse((obj) => sourceObjects.push(obj));
  target.traverse((obj) => targetObjects.push(obj));
  const count = Math.min(sourceObjects.length, targetObjects.length);
  for (let i = 0; i < count; i++) {
    targetObjects[i].position.copy(sourceObjects[i].position);
    targetObjects[i].quaternion.copy(sourceObjects[i].quaternion);
    targetObjects[i].scale.copy(sourceObjects[i].scale);
  }
}

function rebuildGhosts() {
  clearGhosts();
  if (state.settings.ghostUSDInSource && state.usdObject) {
    state.usdGhost = makeGhostObject(state.usdObject, 0x70c8ff);
    sourceView.ghostRoot.add(state.usdGhost);
  }
  if (state.settings.ghostSourceInUSD && state.robot) {
    state.sourceGhost = makeGhostObject(state.robot, 0xffb35c);
    usdView.ghostRoot.add(state.sourceGhost);
  }
  applySceneOrientation();
}

function syncGhosts() {
  syncObjectTransforms(state.usdObject, state.usdGhost);
  syncObjectTransforms(state.robot, state.sourceGhost);
}

function isCollisionObject(object) {
  // Prefer the explicit classification flag when present (set by the MJCF/URDF
  // parser and the converted-USD preview). The name/path heuristic is only a
  // fallback for imported USD without the flag — otherwise geoms legitimately
  // named "*_collision" but tagged group=0 (visual), e.g. iit_softfoot, would be
  // wrongly hidden.
  if (typeof object.userData?.urdfCollision === 'boolean') {
    return object.userData.urdfCollision;
  }
  const marker = [
    object.name,
    object.userData?.nodeCategory,
    object.userData?.['primMeta.absPath']
  ].filter(Boolean).join(' ').toLowerCase();
  return marker.includes('collision') || marker.includes('collider');
}

function pathInSetOrUnder(path, paths) {
  if (!path || !paths) return false;
  if (paths.has(path)) return true;
  for (const candidate of paths) {
    if (path.startsWith(`${candidate}/`)) return true;
  }
  return false;
}

function usdGeometryClassification(extracted) {
  const visualPaths = new Set();
  const collisionPaths = new Set();
  for (const prim of extracted?.prims || []) {
    if (!prim.geometry) continue;
    const group = Number(prim.properties?.['mjc:group']);
    // Three independent collider signals, in priority order:
    //   1. `UsdPhysicsCollisionAPI` applied (canonical UsdPhysics)
    //   2. `mjc:group >= 3` (menagerie convention: groups 3-5 are collision)
    //   3. `purpose == "guide"` (surfaced by the patched
    //      `AppendPhysicsPrimJson` in `web/binding.cc`; used by the
    //      mujoco-usd-converter to hide colliders from default renders)
    // Any of these flips the prim into the collision bucket.
    if (hasApi(prim, 'PhysicsCollisionAPI')
        || (Number.isFinite(group) && group >= 3)
        || prim.purpose === 'guide') {
      collisionPaths.add(prim.path);
    } else {
      visualPaths.add(prim.path);
    }
  }
  return { visualPaths, collisionPaths };
}

function annotateUSDRenderableClasses(root, extracted) {
  const { visualPaths, collisionPaths } = usdGeometryClassification(extracted);
  root?.traverse((obj) => {
    if (!obj.isMesh) return;
    const path = usdPathForObject(obj);
    if (pathInSetOrUnder(path, collisionPaths)) {
      obj.userData.urdfCollision = true;
    } else if (pathInSetOrUnder(path, visualPaths)) {
      obj.userData.urdfCollision = false;
    }
  });
}

function sanitizeUSDIdentifier(name, fallback = 'link') {
  let out = String(name || '').replace(/[^A-Za-z0-9_]/g, '_');
  if (!out) out = fallback;
  if (!/^[A-Za-z_]/.test(out)) out = `_${out}`;
  return out;
}

function findObjectByName(root, name) {
  let found = null;
  root?.traverse((obj) => {
    if (!found && obj.name === name) found = obj;
  });
  return found;
}

function sourceLinkObject(linkName) {
  return state.robot?.links?.[linkName] || null;
}

function captureSourceRestLinkMatrices() {
  state.sourceRestLinkMatrices.clear();
  if (!state.robot?.links) return;
  state.robot.updateWorldMatrix(true, true);
  for (const [name, link] of Object.entries(state.robot.links)) {
    link.updateWorldMatrix(true, false);
    state.sourceRestLinkMatrices.set(name, link.matrixWorld.clone());
  }
}

function bindConvertedUSDLinksToSource() {
  state.usdLinkBindings = [];
  if (!state.robot?.links || !state.usdObject || !state.exportPayload?.links) return;

  state.robot.updateWorldMatrix(true, true);
  state.usdObject.updateWorldMatrix(true, true);
  for (const link of state.exportPayload.links) {
    const linkName = link.name;
    const sourceLink = sourceLinkObject(linkName);
    const usdLink = findObjectByName(state.usdObject, sanitizeUSDIdentifier(linkName, 'link'));
    if (!sourceLink || !usdLink) continue;
    const sourceRestWorld = state.sourceRestLinkMatrices.get(linkName) || sourceLink.matrixWorld.clone();
    state.usdLinkBindings.push({
      linkName,
      sourceLink,
      usdLink,
      sourceRestWorld: sourceRestWorld.clone(),
      inverseSourceRestWorld: sourceRestWorld.clone().invert(),
      usdRestWorld: usdLink.matrixWorld.clone()
    });
  }
}

function syncConvertedUSDToSourcePose() {
  if (!state.usdLinkBindings.length) return;
  state.robot?.updateWorldMatrix(true, true);
  state.usdObject?.updateWorldMatrix(true, true);
  for (const binding of state.usdLinkBindings) {
    binding.sourceLink.updateWorldMatrix(true, false);
    const delta = new THREE.Matrix4()
      .copy(binding.sourceLink.matrixWorld)
      .multiply(binding.inverseSourceRestWorld);
    const desiredWorld = new THREE.Matrix4().copy(delta).multiply(binding.usdRestWorld);
    const parentInverse = new THREE.Matrix4();
    if (binding.usdLink.parent) {
      binding.usdLink.parent.updateWorldMatrix(true, false);
      parentInverse.copy(binding.usdLink.parent.matrixWorld).invert();
    } else {
      parentInverse.identity();
    }
    const local = parentInverse.multiply(desiredWorld);
    local.decompose(binding.usdLink.position, binding.usdLink.quaternion, binding.usdLink.scale);
    binding.usdLink.updateWorldMatrix(false, true);
  }
}

function worldPositionOf(object) {
  object.updateWorldMatrix(true, false);
  return new THREE.Vector3().setFromMatrixPosition(object.matrixWorld);
}

function objectWorldCenter(object) {
  const box = new THREE.Box3().setFromObject(object);
  if (!box.isEmpty()) return box.getCenter(new THREE.Vector3());
  return worldPositionOf(object);
}

function makeLinkDebugEntry(object) {
  return {
    origin: worldPositionOf(object),
    center: objectWorldCenter(object),
    object
  };
}

function sourceLinkDebugEntries() {
  const entries = new Map();
  if (!state.robot?.links) return entries;
  state.robot.updateWorldMatrix(true, true);
  for (const [name, link] of Object.entries(state.robot.links)) {
    entries.set(name, makeLinkDebugEntry(link));
  }
  return entries;
}

function usdLinkDebugEntries() {
  if (state.usdLinkBindings.length) {
    const entries = new Map();
    state.usdObject?.updateWorldMatrix(true, true);
    for (const binding of state.usdLinkBindings) {
      entries.set(binding.linkName, makeLinkDebugEntry(binding.usdLink));
    }
    return entries;
  }

  const entries = new Map();
  const linksScope = findObjectByName(state.usdObject, 'Links');
  if (linksScope) {
    linksScope.updateWorldMatrix(true, true);
    for (const child of linksScope.children) {
      entries.set(child.name, makeLinkDebugEntry(child));
    }
  } else if (state.usdArticulation?.links) {
    state.usdArticulation.updateWorldMatrix(true, true);
    for (const [name, link] of Object.entries(state.usdArticulation.links)) {
      entries.set(name, makeLinkDebugEntry(link));
    }
  }
  return entries;
}

function jointPosition(joint, linkEntries) {
  if (joint?.pivot) return worldPositionOf(joint.pivot);
  if (joint?.isObject3D) return worldPositionOf(joint);
  if (joint?.child && linkEntries.has(joint.child)) return linkEntries.get(joint.child).origin.clone();
  return null;
}

function axisVector(axis) {
  if (axis?.isVector3) return axis.clone();
  if (Array.isArray(axis)) return new THREE.Vector3(axis[0] || 0, axis[1] || 0, axis[2] || 0);
  if (axis && typeof axis === 'object') return new THREE.Vector3(axis.x || 0, axis.y || 0, axis.z || 0);
  return new THREE.Vector3(1, 0, 0);
}

function jointWorldDirection(joint, linkEntries) {
  const direction = axisVector(joint?.axis);
  if (direction.lengthSq() < 1.0e-12) direction.set(1, 0, 0);
  direction.normalize();

  const frame = joint?.pivot || (joint?.isObject3D ? joint : linkEntries.get(joint?.child)?.object);
  if (frame?.isObject3D) {
    const worldQuat = new THREE.Quaternion();
    frame.getWorldQuaternion(worldQuat);
    direction.applyQuaternion(worldQuat).normalize();
  }
  return direction;
}

function debugVisualizationEnabled() {
  return state.settings.showLinkLines
    || state.settings.showJointArrows
    || state.settings.showLinkNames
    || state.settings.showJointNames;
}

function makeTextSprite(text, color) {
  const canvas = document.createElement('canvas');
  const ctx = canvas.getContext('2d');
  const fontSize = 36;
  const paddingX = 14;
  const paddingY = 8;
  ctx.font = `600 ${fontSize}px system-ui, sans-serif`;
  const metrics = ctx.measureText(text);
  canvas.width = Math.ceil(metrics.width + paddingX * 2);
  canvas.height = fontSize + paddingY * 2;

  ctx.font = `600 ${fontSize}px system-ui, sans-serif`;
  ctx.textBaseline = 'middle';
  ctx.fillStyle = 'rgba(12, 16, 22, 0.72)';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.22)';
  ctx.strokeRect(0.5, 0.5, canvas.width - 1, canvas.height - 1);
  ctx.fillStyle = color;
  ctx.fillText(text, paddingX, canvas.height * 0.5);

  const texture = new THREE.CanvasTexture(canvas);
  texture.userData.debugLabelTexture = true;
  const material = new THREE.SpriteMaterial({
    map: texture,
    transparent: true,
    depthTest: false,
    depthWrite: false
  });
  const sprite = new THREE.Sprite(material);
  const height = 0.018;
  sprite.scale.set(height * (canvas.width / canvas.height), height, 1);
  sprite.renderOrder = 20;
  return sprite;
}

function addLabel(root, text, position, color, offsetY = 0.035) {
  if (!text || !position) return;
  const label = makeTextSprite(text, color);
  label.position.copy(position);
  label.position.y += offsetY;
  root.add(label);
}

function jointsByParentLink(joints) {
  const byParent = new Map();
  for (const joint of joints) {
    if (!joint.parent) continue;
    if (!byParent.has(joint.parent)) byParent.set(joint.parent, []);
    byParent.get(joint.parent).push(joint);
  }
  return byParent;
}

function makeLinkSegments(linkEntries, joints) {
  const segments = [];
  const byParent = jointsByParentLink(joints);
  for (const joint of joints) {
    if (!joint.child) continue;
    const start = jointPosition(joint, linkEntries);
    if (!start) continue;
    const nextJoints = byParent.get(joint.child) || [];
    if (nextJoints.length) {
      for (const nextJoint of nextJoints) {
        const end = jointPosition(nextJoint, linkEntries);
        if (end && start.distanceToSquared(end) > 1.0e-10) {
          segments.push({ linkName: joint.child, start, end });
        }
      }
      continue;
    }

    const entry = linkEntries.get(joint.child);
    const end = entry?.center || entry?.origin;
    if (end && start.distanceToSquared(end) > 1.0e-10) {
      segments.push({ linkName: joint.child, start, end });
    }
  }
  return segments;
}

function makeJointArrow(position, direction, material) {
  const arrow = new THREE.Group();
  arrow.position.copy(position);
  arrow.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), direction.clone().normalize());
  arrow.renderOrder = 10;

  const shaft = new THREE.Mesh(jointArrowShaftGeometry.clone(), material.clone());
  shaft.position.y = 0.039;
  shaft.renderOrder = 10;
  arrow.add(shaft);

  const head = new THREE.Mesh(jointArrowHeadGeometry.clone(), material.clone());
  head.position.y = 0.098;
  head.renderOrder = 10;
  arrow.add(head);
  return arrow;
}

function buildSkeletonDebug(root, linkEntries, joints, lineMaterial, jointMaterial, labelColor) {
  clearDebugRoot(root);
  if (!debugVisualizationEnabled()) return;

  const segments = makeLinkSegments(linkEntries, joints);

  if (state.settings.showLinkLines) {
    const points = [];
    for (const segment of segments) {
      points.push(segment.start.x, segment.start.y, segment.start.z, segment.end.x, segment.end.y, segment.end.z);
    }
    if (points.length) {
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute('position', new THREE.Float32BufferAttribute(points, 3));
      root.add(new THREE.LineSegments(geometry, lineMaterial.clone()));
    }
  }

  if (state.settings.showLinkNames) {
    for (const segment of segments) {
      const midpoint = new THREE.Vector3().addVectors(segment.start, segment.end).multiplyScalar(0.5);
      addLabel(root, segment.linkName, midpoint, labelColor);
    }
  }

  if (state.settings.showJointArrows) {
    for (const joint of joints) {
      const type = joint.jointType || joint.type || 'fixed';
      if (type === 'fixed') continue;
      const pos = jointPosition(joint, linkEntries);
      if (!pos) continue;
      root.add(makeJointArrow(pos, jointWorldDirection(joint, linkEntries), jointMaterial));
    }
  }

  if (state.settings.showJointNames) {
    for (const joint of joints) {
      const pos = jointPosition(joint, linkEntries);
      addLabel(root, joint.name || joint.jointName || 'joint', pos, labelColor, 0.055);
    }
  }
}

function updateSkeletonDebugVisualizations() {
  if (!debugVisualizationEnabled()) {
    clearDebugVisualizations();
    return;
  }
  const joints = Object.values(state.joints || {});
  buildSkeletonDebug(sourceView.debugRoot, sourceLinkDebugEntries(), joints, sourceLinkLineMaterial, sourceJointMaterial, sourceLabelColor);
  buildSkeletonDebug(usdView.debugRoot, usdLinkDebugEntries(), joints, usdLinkLineMaterial, usdJointMaterial, usdLabelColor);
}

async function parseURDFWithMeshes(urdfText, filename) {
  const doc = parseXMLDocument(urdfText);
  if (doc.documentElement?.localName !== 'robot') {
    throw new Error(`Expected URDF <robot> root, got <${doc.documentElement?.localName || 'unknown'}>.`);
  }

  const manager = new THREE.LoadingManager();
  manager.setURLModifier((url) => {
    const file = resolveAssetFile(url);
    return file ? objectURLForFile(file) : url;
  });

  const loader = new URDFLoader(manager);
  loader.parseCollision = true;
  loader.packages = (pkg) => pkg;
  loader.loadMeshCb = (path, _manager, done) => {
    loadURDFMesh(path)
      .then((obj) => done(obj))
      .catch((err) => {
        console.error(err);
        done(makeMissingMesh(path));
      });
  };

  const robot = loader.parse(urdfText);
  robot.name = robot.name || filename.replace(/\.[^.]+$/, '');
  return robot;
}

function parseXMLDocument(text) {
  const doc = new DOMParser().parseFromString(text, 'application/xml');
  const parseError = doc.querySelector('parsererror');
  if (parseError) {
    throw new Error(parseError.textContent?.trim() || 'XML parse failed.');
  }
  return doc;
}

function detectInputFormat(text) {
  const rootName = parseXMLDocument(text).documentElement?.localName || '';
  if (rootName === 'robot') return 'urdf';
  if (rootName === 'mujoco') return 'mjcf';
  throw new Error(`Unsupported XML root <${rootName || 'unknown'}>. Expected <robot> or <mujoco>.`);
}

function decodeXML(text) {
  return text
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

function parseAttributes(text = '') {
  const attrs = {};
  const re = /([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;
  let match = null;
  while ((match = re.exec(text))) {
    attrs[match[1]] = decodeXML(match[2] ?? match[3] ?? '');
  }
  return attrs;
}

function stripMujocoDocumentRoot(xml) {
  const withoutDecl = xml.replace(/<\?xml[\s\S]*?\?>/i, '');
  const open = withoutDecl.search(/<mujoco\b[^>]*>/i);
  const close = withoutDecl.lastIndexOf('</mujoco>');
  if (open < 0 || close < 0) return withoutDecl;
  const openEnd = withoutDecl.indexOf('>', open);
  return withoutDecl.slice(openEnd + 1, close);
}

async function expandMujocoIncludes(xml, baseDir = '', seen = new Set()) {
  const includeRe = /<include\b([^>]*?)\/\s*>/gi;
  let result = '';
  let lastIndex = 0;
  let match = null;
  while ((match = includeRe.exec(xml))) {
    result += xml.slice(lastIndex, match.index);
    lastIndex = includeRe.lastIndex;

    const attrs = parseAttributes(match[1] || '');
    if (!attrs.file) continue;

    const includePath = joinPath(baseDir, attrs.file);
    const entry = resolveAssetEntry(includePath) || resolveAssetEntry(attrs.file);
    if (!entry) {
      throw new Error(`MJCF include not found: ${attrs.file}. Select the model folder with Assets first.`);
    }
    if (seen.has(entry.rel)) {
      throw new Error(`Recursive MJCF include: ${entry.rel}`);
    }

    seen.add(entry.rel);
    const childXML = stripMujocoDocumentRoot(await entry.file.text());
    result += await expandMujocoIncludes(childXML, dirname(entry.rel), seen);
    seen.delete(entry.rel);
  }
  result += xml.slice(lastIndex);
  return result;
}

function parseNumbers(text, fallback = []) {
  if (!text) return fallback;
  const values = text.trim().split(/\s+/).map(Number).filter(Number.isFinite);
  return values.length ? values : fallback;
}

function parseURDFMetadata(text) {
  const doc = new DOMParser().parseFromString(text, 'application/xml');
  const robotEl = doc.querySelector('robot');
  const links = new Map();
  const joints = [];

  for (const linkEl of doc.querySelectorAll('link')) {
    const name = linkEl.getAttribute('name') || `link_${links.size}`;
    const inertialEl = linkEl.querySelector(':scope > inertial');
    const inertial = {};
    if (inertialEl) {
      const massEl = inertialEl.querySelector(':scope > mass');
      const originEl = inertialEl.querySelector(':scope > origin');
      const inertiaEl = inertialEl.querySelector(':scope > inertia');
      if (massEl) inertial.mass = Number(massEl.getAttribute('value')) || 0;
      if (originEl) inertial.centerOfMass = parseNumbers(originEl.getAttribute('xyz'), [0, 0, 0]);
      if (inertiaEl) {
        inertial.diagonalInertia = [
          Number(inertiaEl.getAttribute('ixx')) || 0,
          Number(inertiaEl.getAttribute('iyy')) || 0,
          Number(inertiaEl.getAttribute('izz')) || 0
        ];
      }
    }
    links.set(name, { name, inertial });
  }

  for (const jointEl of doc.querySelectorAll('joint')) {
    const parent = jointEl.querySelector(':scope > parent')?.getAttribute('link') || '';
    const child = jointEl.querySelector(':scope > child')?.getAttribute('link') || '';
    const axis = parseNumbers(jointEl.querySelector(':scope > axis')?.getAttribute('xyz'), [1, 0, 0]);
    const origin = parseNumbers(jointEl.querySelector(':scope > origin')?.getAttribute('xyz'), [0, 0, 0]);
    const rpy = parseNumbers(jointEl.querySelector(':scope > origin')?.getAttribute('rpy'), [0, 0, 0]);
    const originQuat = new THREE.Quaternion().setFromEuler(new THREE.Euler(rpy[0] || 0, rpy[1] || 0, rpy[2] || 0, 'XYZ'));
    const limitEl = jointEl.querySelector(':scope > limit');
    const dynamicsEl = jointEl.querySelector(':scope > dynamics');
    joints.push({
      name: jointEl.getAttribute('name') || `joint_${joints.length}`,
      type: jointEl.getAttribute('type') || 'fixed',
      parent,
      child,
      axis,
      axisToken: axisToToken(axis),
      origin,
      originMatrix: originToUSDMatrix(origin, rpy),
      localPos0: origin,
      localPos1: [0, 0, 0],
      localRot0: quaternionToUSDArray(originQuat),
      localRot1: [1, 0, 0, 0],
      limit: limitEl ? {
        lower: Number(limitEl.getAttribute('lower')),
        upper: Number(limitEl.getAttribute('upper')),
        effort: Number(limitEl.getAttribute('effort')),
        velocity: Number(limitEl.getAttribute('velocity'))
      } : {},
      dynamics: dynamicsEl ? {
        damping: Number(dynamicsEl.getAttribute('damping')),
        friction: Number(dynamicsEl.getAttribute('friction'))
      } : {}
    });
  }

  return {
    name: robotEl?.getAttribute('name') || '',
    links,
    joints
  };
}

function axisToToken(axis) {
  const abs = axis.map((v) => Math.abs(v));
  const max = Math.max(abs[0] || 0, abs[1] || 0, abs[2] || 0);
  if (max === abs[1]) return 'Y';
  if (max === abs[2]) return 'Z';
  return 'X';
}

function originToUSDMatrix(xyz, rpy) {
  const euler = new THREE.Euler(rpy[0] || 0, rpy[1] || 0, rpy[2] || 0, 'XYZ');
  const matrix = new THREE.Matrix4().compose(
    new THREE.Vector3(xyz[0] || 0, xyz[1] || 0, xyz[2] || 0),
    new THREE.Quaternion().setFromEuler(euler),
    new THREE.Vector3(1, 1, 1)
  );
  return matrixToUSDArray(matrix);
}

function childElements(node, name = null) {
  return Array.from(node?.children || []).filter((child) => !name || child.localName === name);
}

function firstChildElement(node, name) {
  return childElements(node, name)[0] || null;
}

function numberAttr(nodeOrAttrs, name, fallback = undefined) {
  const raw = typeof nodeOrAttrs?.getAttribute === 'function'
    ? nodeOrAttrs.getAttribute(name)
    : nodeOrAttrs?.[name];
  const value = Number(raw);
  return Number.isFinite(value) ? value : fallback;
}

function attrsFromElement(el) {
  const attrs = {};
  for (const attr of Array.from(el?.attributes || [])) attrs[attr.name] = attr.value;
  return attrs;
}

// MuJoCo <compiler> context for angle units + euler sequence. Set per-parse in
// parseMJCFWithMeshes; defaults match MuJoCo (degrees, "xyz").
let mjcfPoseCtx = { toRad: Math.PI / 180, eulerseq: 'xyz' };

function eulerQuatFromSeq(angles, seq, toRad) {
  const axisFor = (c) => (c === 'x' ? new THREE.Vector3(1, 0, 0)
    : c === 'y' ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(0, 0, 1));
  const q = new THREE.Quaternion();
  for (let i = 0; i < seq.length && i < angles.length; i++) {
    const c = seq[i];
    const lower = c.toLowerCase();
    const qi = new THREE.Quaternion().setFromAxisAngle(axisFor(lower), (angles[i] || 0) * toRad);
    if (c === lower) q.multiply(qi);   // lowercase = intrinsic (moving axes)
    else q.premultiply(qi);            // uppercase = extrinsic (fixed axes)
  }
  return q;
}

// Resolve any MuJoCo orientation specifier (quat/axisangle/euler/xyaxes/zaxis).
function orientationQuat(attrs, ctx) {
  const toRad = ctx.toRad;
  if (attrs.quat) {
    const q = parseNumbers(attrs.quat, [1, 0, 0, 0]);
    return new THREE.Quaternion(q[1] || 0, q[2] || 0, q[3] || 0, q[0] ?? 1).normalize();
  }
  if (attrs.axisangle) {
    const a = parseNumbers(attrs.axisangle, [0, 0, 1, 0]);
    const axis = new THREE.Vector3(a[0] || 0, a[1] || 0, a[2] || 0);
    if (axis.lengthSq() < 1e-12) axis.set(0, 0, 1);
    return new THREE.Quaternion().setFromAxisAngle(axis.normalize(), (a[3] || 0) * toRad);
  }
  if (attrs.euler) {
    return eulerQuatFromSeq(parseNumbers(attrs.euler, [0, 0, 0]), ctx.eulerseq, toRad);
  }
  if (attrs.xyaxes) {
    const v = parseNumbers(attrs.xyaxes, [1, 0, 0, 0, 1, 0]);
    const x = new THREE.Vector3(v[0], v[1], v[2]);
    if (x.lengthSq() < 1e-12) x.set(1, 0, 0);
    x.normalize();
    const y = new THREE.Vector3(v[3], v[4], v[5]);
    y.sub(x.clone().multiplyScalar(x.dot(y)));   // Gram-Schmidt against x
    if (y.lengthSq() < 1e-12) y.crossVectors(new THREE.Vector3(0, 0, 1), x);
    y.normalize();
    const z = new THREE.Vector3().crossVectors(x, y);
    return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
  }
  if (attrs.zaxis) {
    const v = parseNumbers(attrs.zaxis, [0, 0, 1]);
    const z = new THREE.Vector3(v[0] || 0, v[1] || 0, v[2] || 0);
    if (z.lengthSq() < 1e-12) z.set(0, 0, 1);
    return new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 0, 1), z.normalize());
  }
  return new THREE.Quaternion();
}

function matrixFromPoseAttrs(attrs = {}) {
  const pos = parseNumbers(attrs.pos, [0, 0, 0]);
  const translation = new THREE.Vector3(pos[0] || 0, pos[1] || 0, pos[2] || 0);
  const quat = orientationQuat(attrs, mjcfPoseCtx);
  return new THREE.Matrix4().compose(translation, quat, new THREE.Vector3(1, 1, 1));
}

function decomposeMatrix(matrix) {
  const position = new THREE.Vector3();
  const quaternion = new THREE.Quaternion();
  const scale = new THREE.Vector3();
  matrix.decompose(position, quaternion, scale);
  return { position, quaternion, scale };
}

function quaternionToUSDArray(quaternion) {
  return [quaternion.w, quaternion.x, quaternion.y, quaternion.z];
}

function transformPointArray(matrix, values = [0, 0, 0]) {
  const point = vectorFromArray(values);
  point.applyMatrix4(matrix);
  return point.toArray();
}

function applyMatrixToObject(object, matrix) {
  matrix.decompose(object.position, object.quaternion, object.scale);
}

function collectMujocoAssets(root) {
  const compiler = firstChildElement(root, 'compiler');
  // <compiler meshdir> wins; assetdir is the shared meshdir/texturedir default.
  const meshDir = compiler?.getAttribute('meshdir') || compiler?.getAttribute('assetdir') || '';
  const meshes = new Map();
  for (const asset of childElements(root, 'asset')) {
    for (const mesh of childElements(asset, 'mesh')) {
      const file = mesh.getAttribute('file') || '';
      if (!file) continue;
      const name = mesh.getAttribute('name') || file.split('/').pop().replace(/\.[^.]+$/, '');
      meshes.set(name, {
        path: joinPath(meshDir, file),
        scale: parseNumbers(mesh.getAttribute('scale'), [1, 1, 1]),
        refpos: parseNumbers(mesh.getAttribute('refpos'), [0, 0, 0]),
        refquat: parseNumbers(mesh.getAttribute('refquat'), [1, 0, 0, 0])
      });
    }
  }
  return meshes;
}

function collectMujocoDefaults(root) {
  const defaults = {
    geom: new Map(),
    joint: new Map()
  };

  function mergeAttrs(base, attrs) {
    return { ...(base || {}), ...(attrs || {}) };
  }

  function visitDefault(defaultNode, inherited = { geom: {}, joint: {} }) {
    const className = defaultNode.getAttribute('class') || '';
    const next = {
      geom: mergeAttrs(inherited.geom, attrsFromElement(firstChildElement(defaultNode, 'geom'))),
      joint: mergeAttrs(inherited.joint, attrsFromElement(firstChildElement(defaultNode, 'joint')))
    };

    if (className) {
      defaults.geom.set(className, next.geom);
      defaults.joint.set(className, next.joint);
    } else {
      defaults.geom.set('', next.geom);
      defaults.joint.set('', next.joint);
    }

    for (const child of childElements(defaultNode, 'default')) {
      visitDefault(child, next);
    }
  }

  for (const defaultNode of childElements(root, 'default')) {
    visitDefault(defaultNode);
  }
  return defaults;
}

function resolveMujocoAttrs(node, defaults, kind, inheritedClass = '') {
  const attrs = attrsFromElement(node);
  const className = attrs.class || inheritedClass || '';
  return {
    ...(defaults?.[kind]?.get('') || {}),
    ...(className ? defaults?.[kind]?.get(className) || {} : {}),
    ...attrs,
    class: className || attrs.class
  };
}

function makeGeometryPayload(mesh, matrix, name) {
  const geom = mesh.geometry;
  const pos = geom?.getAttribute('position');
  if (!pos || pos.count < 3) return null;
  const normal = geom.getAttribute('normal');
  const uv = geom.getAttribute('uv');
  const index = geom.getIndex();
  return {
    name,
    matrix: matrixToUSDArray(matrix),
    geometry: {
      positions: Array.from(pos.array),
      normals: normal ? Array.from(normal.array) : [],
      uvs: uv ? Array.from(uv.array) : [],
      indices: index ? Array.from(index.array) : []
    }
  };
}

function collectMeshPayloads(root, baseMatrix, fallbackName) {
  const payloads = [];
  root.updateMatrixWorld(true);
  let index = 0;
  root.traverse((obj) => {
    if (!obj.isMesh) return;
    const matrix = new THREE.Matrix4().copy(baseMatrix).multiply(obj.matrixWorld);
    const payload = makeGeometryPayload(obj, matrix, obj.name || `${fallbackName}_${index}`);
    if (payload) {
      payload.name = payloads.length ? `${payload.name}_${payloads.length}` : payload.name;
      payloads.push(payload);
    }
    index++;
  });
  return payloads;
}

function asFloat32Array(values) {
  return values instanceof Float32Array ? values : new Float32Array(values || []);
}

function asInt32Array(values) {
  return values instanceof Int32Array ? values : new Int32Array(values || []);
}

function asUint32Array(values) {
  return values instanceof Uint32Array ? values : new Uint32Array(values || []);
}

function nativeMeshBufferForMesh(mesh, meshRef) {
  if (state.nativeMeshBuffers.has(meshRef)) return meshRef;
  const geom = mesh.geometry;
  const pos = geom?.getAttribute('position');
  if (!pos || pos.count < 3) return null;
  const normal = geom.getAttribute('normal');
  const uv = geom.getAttribute('uv');
  const index = geom.getIndex();
  let indices = null;
  if (index) {
    indices = asInt32Array(index.array);
  } else {
    indices = new Int32Array(pos.count);
    for (let i = 0; i < indices.length; i++) indices[i] = i;
  }
  state.nativeMeshBuffers.set(meshRef, {
    positions: asFloat32Array(pos.array),
    normals: normal ? asFloat32Array(normal.array) : new Float32Array(),
    uvs: uv ? asFloat32Array(uv.array) : new Float32Array(),
    indices
  });
  return meshRef;
}

function collectNativeMeshRefPayloads(root, baseMatrix, fallbackName, meshRefPrefix) {
  const payloads = [];
  root.updateMatrixWorld(true);
  let index = 0;
  root.traverse((obj) => {
    if (!obj.isMesh) return;
    const meshRef = sanitizeUSDIdentifier(`${meshRefPrefix}_${obj.name || index}`, 'mesh');
    if (!nativeMeshBufferForMesh(obj, meshRef)) return;
    const matrix = new THREE.Matrix4().copy(baseMatrix).multiply(obj.matrixWorld);
    payloads.push({
      name: payloads.length ? `${fallbackName}_${payloads.length}` : fallbackName,
      matrix: matrixToUSDArray(matrix),
      meshRef
    });
    index++;
  });
  return payloads;
}

function primitiveObjectFromGeometry(geometry, name) {
  const mesh = new THREE.Mesh(
    geometry,
    new THREE.MeshStandardMaterial({ color: 0x9fb6c7, roughness: 0.68, metalness: 0.02 })
  );
  mesh.name = name;
  const group = new THREE.Group();
  group.name = name;
  group.add(mesh);
  return group;
}

// Merge every sub-mesh of `object` (e.g. an OBJ split into many `o`/`g`
// objects, as in ms_human_700's Rib1L.obj) into a single mesh. A MuJoCo
// <mesh> is one mesh regardless of how the source file is grouped, matching
// the native loader / JS CLI and MuJoCo semantics; keeps the demo's source
// view and exported USD mesh counts consistent with the CLI.
function flattenToSingleMesh(object, name) {
  object.updateMatrixWorld(true);
  const positions = [];
  const normals = [];
  const uvs = [];
  const indices = [];
  let base = 0;
  let hasNormals = true;
  let hasUVs = true;
  const v = new THREE.Vector3();
  const nrm = new THREE.Vector3();
  let meshCount = 0;
  object.traverse((obj) => {
    if (!obj.isMesh || !obj.geometry) return;
    meshCount++;
    const g = obj.geometry;
    const pos = g.getAttribute('position');
    if (!pos) return;
    const nAttr = g.getAttribute('normal');
    const uvAttr = g.getAttribute('uv');
    const idx = g.getIndex();
    const m = obj.matrixWorld;
    const normalMat = new THREE.Matrix3().getNormalMatrix(m);
    for (let i = 0; i < pos.count; i++) {
      v.set(pos.getX(i), pos.getY(i), pos.getZ(i)).applyMatrix4(m);
      positions.push(v.x, v.y, v.z);
      if (nAttr) {
        nrm.set(nAttr.getX(i), nAttr.getY(i), nAttr.getZ(i)).applyMatrix3(normalMat).normalize();
        normals.push(nrm.x, nrm.y, nrm.z);
      } else {
        hasNormals = false;
      }
      if (uvAttr) uvs.push(uvAttr.getX(i), uvAttr.getY(i)); else hasUVs = false;
    }
    if (idx) {
      for (let i = 0; i < idx.count; i++) indices.push(base + idx.getX(i));
    } else {
      for (let i = 0; i < pos.count; i++) indices.push(base + i);
    }
    base += pos.count;
  });
  if (meshCount <= 1) return object;  // nothing to merge
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  if (hasNormals && normals.length) geom.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  if (hasUVs && uvs.length) geom.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geom.setIndex(indices);
  return primitiveObjectFromGeometry(geom, name);
}

function mujocoJointType(type) {
  if (type === 'hinge') return 'revolute';
  if (type === 'slide') return 'prismatic';
  if (type === 'free') return 'floating';
  return 'fixed';
}

function parseMujocoInertial(bodyNode) {
  const inertialNode = firstChildElement(bodyNode, 'inertial');
  if (!inertialNode) return {};
  const full = parseNumbers(inertialNode.getAttribute('fullinertia'), []);
  const inertial = {
    mass: numberAttr(inertialNode, 'mass', 0),
    centerOfMass: parseNumbers(inertialNode.getAttribute('pos'), [0, 0, 0])
  };
  if (full.length >= 3) {
    inertial.diagonalInertia = [full[0], full[1], full[2]];
  } else {
    inertial.diagonalInertia = parseNumbers(inertialNode.getAttribute('diaginertia'), []);
  }
  return inertial;
}

// MuJoCo capsule/cylinder fromto="x1 y1 z1 x2 y2 z2": the geom spans p1->p2 with
// radius=size[0]; pos/quat are ignored when fromto is present. Returns the local
// center, segment length, and the rotation aligning the +Y axis (three.js
// cylinder axis) to the segment direction.
function mujocoFromto(attrs) {
  const ft = parseNumbers(attrs?.fromto, []);
  if (ft.length < 6) return null;
  const p1 = new THREE.Vector3(ft[0], ft[1], ft[2]);
  const p2 = new THREE.Vector3(ft[3], ft[4], ft[5]);
  const dir = p2.clone().sub(p1);
  const length = dir.length() || 1e-6;
  const ndir = dir.clone().normalize();
  return {
    center: p1.clone().add(p2).multiplyScalar(0.5),
    length,
    dir: ndir,
    quatY: new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), ndir),
    quatZ: new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 0, 1), ndir)
  };
}

// MuJoCo mesh refpos/refquat define the mesh's reference frame: vertices are
// translated by -refpos and rotated by the conjugate of refquat before the geom
// places them. Returns the corresponding local matrix (identity by default).
function mujocoMeshRefMatrix(meshAsset) {
  const rq = meshAsset?.refquat || [1, 0, 0, 0];
  const rp = meshAsset?.refpos || [0, 0, 0];
  const q = new THREE.Quaternion(rq[1] || 0, rq[2] || 0, rq[3] || 0, rq[0] ?? 1).normalize().conjugate();
  return new THREE.Matrix4().makeRotationFromQuaternion(q)
    .multiply(new THREE.Matrix4().makeTranslation(-(rp[0] || 0), -(rp[1] || 0), -(rp[2] || 0)));
}

function shapePayloadForMujocoGeom(geomAttrs, originMatrix, fallbackName) {
  const attrs = geomAttrs || {};
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  if (geomType === 'mesh') return null;

  const size = parseNumbers(attrs.size, []);
  if (geomType === 'box') {
    const matrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(size[0] || 1, size[1] || 1, size[2] || 1));
    return [{ name: fallbackName, matrix: matrixToUSDArray(matrix), shape: { type: 'box' } }];
  }

  if (geomType === 'sphere') {
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: { type: 'sphere', radius: size[0] || 0.5 }
    }];
  }

  if (geomType === 'cylinder' || geomType === 'capsule') {
    // fromto: bake the segment midpoint + Z->direction into the matrix and use
    // the segment length as height (pos/quat ignored when fromto is present).
    const ft = mujocoFromto(attrs);
    const matrix = ft
      ? new THREE.Matrix4().copy(originMatrix)
          .multiply(new THREE.Matrix4().compose(ft.center, ft.quatZ, new THREE.Vector3(1, 1, 1)))
      : originMatrix;
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(matrix),
      shape: {
        type: geomType,
        radius: size[0] || 0.5,
        height: ft ? ft.length : (size[1] ? size[1] * 2 : 1),
        axis: 'Z'
      }
    }];
  }

  if (geomType === 'plane') {
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: {
        type: 'plane',
        width: (size[0] && size[0] > 0 ? size[0] : 1) * 2,
        length: (size[1] && size[1] > 0 ? size[1] : 1) * 2,
        axis: 'Z'
      }
    }];
  }

  return null;
}

async function loadMujocoGeomObject(geomAttrs, meshAssets, fallbackName) {
  const attrs = geomAttrs || {};
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    if (!meshAsset) return makeMissingMesh(attrs.mesh || fallbackName);
    // A MuJoCo <mesh> is a single mesh: merge multi-object source files so the
    // source view's mesh count matches the exported USD and the CLI.
    return flattenToSingleMesh(await loadMeshObject(meshAsset.path), attrs.mesh || fallbackName);
  }

  const size = parseNumbers(attrs.size, []);
  if (geomType === 'box') {
    return primitiveObjectFromGeometry(
      new THREE.BoxGeometry((size[0] || 1) * 2, (size[1] || 1) * 2, (size[2] || 1) * 2),
      fallbackName
    );
  }
  if (geomType === 'sphere') {
    return primitiveObjectFromGeometry(new THREE.SphereGeometry(size[0] || 0.5, 24, 12), fallbackName);
  }
  if (geomType === 'cylinder' || geomType === 'capsule') {
    const radius = size[0] || 0.5;
    const ft = mujocoFromto(attrs);
    const length = ft ? ft.length : (size[1] ? size[1] * 2 : 1);
    return primitiveObjectFromGeometry(new THREE.CylinderGeometry(radius, radius, length, 24, 1), fallbackName);
  }
  if (geomType === 'plane') {
    return primitiveObjectFromGeometry(
      new THREE.PlaneGeometry((size[0] && size[0] > 0 ? size[0] : 1) * 2, (size[1] && size[1] > 0 ? size[1] : 1) * 2),
      fallbackName
    );
  }

  return makeMissingMesh(fallbackName);
}

async function mujocoGeomPayloads(geomAttrs, meshAssets, fallbackName, baseMatrix = new THREE.Matrix4()) {
  const attrs = geomAttrs || {};
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  const originMatrix = new THREE.Matrix4().copy(baseMatrix).multiply(matrixFromPoseAttrs(attrs));
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    if (!meshAsset) return [];
    // Merge multi-object source meshes into one (MuJoCo <mesh> == one mesh).
    const object = flattenToSingleMesh(await loadMeshObject(meshAsset.path), attrs.mesh || fallbackName);
    const scale = parseNumbers(attrs.scale, meshAsset.scale || [1, 1, 1]);
    const meshMatrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(mujocoMeshRefMatrix(meshAsset))
      .multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
    return collectNativeMeshRefPayloads(object, meshMatrix, fallbackName, meshAsset.path || attrs.mesh || fallbackName);
  }

  const object = await loadMujocoGeomObject(attrs, meshAssets, fallbackName);
  const yToZ = geomType === 'cylinder' || geomType === 'capsule'
    ? new THREE.Matrix4().makeRotationX(Math.PI / 2)
    : new THREE.Matrix4();
  return collectMeshPayloads(object, new THREE.Matrix4().copy(originMatrix).multiply(yToZ), fallbackName);
}

function classifyMujocoGeom(geomAttrs) {
  const attrs = geomAttrs || {};
  // MuJoCo visibility is by geom group: 0-2 are the default-visible set, 3-5 are
  // hidden/auxiliary (collision). The resolved group (explicit on the geom, else
  // from its class) is authoritative and wins over the class NAME — e.g.
  // iit_softfoot tags class="collision" meshes with group="0" to show them, and
  // shadow_dexee's group-5 CollisionGeom capsules must stay hidden.
  const g = Number(attrs.group);
  if (Number.isFinite(g)) return g < 3;
  // No group => MuJoCo default group 0 (visible); use the class name only as a
  // hint to keep ungrouped collision-class geoms hidden.
  const className = String(attrs.class || '').toLowerCase();
  if (className.includes('collision') || className.includes('collider')) return false;
  return true;
}

function applyMujocoObjectDisplayTransform(object, geomAttrs, meshAssets) {
  const attrs = geomAttrs || {};
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  // fromto fully specifies a capsule/cylinder's placement (pos/quat ignored):
  // center it on the segment midpoint and align the cylinder's +Y axis to it.
  if (geomType === 'cylinder' || geomType === 'capsule') {
    const ft = mujocoFromto(attrs);
    if (ft) {
      applyMatrixToObject(object, new THREE.Matrix4().compose(ft.center, ft.quatY, new THREE.Vector3(1, 1, 1)));
      return;
    }
  }
  const matrix = matrixFromPoseAttrs(attrs);
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    const scale = parseNumbers(attrs.scale, meshAsset?.scale || [1, 1, 1]);
    // Apply the mesh reference frame (refpos/refquat) before scale, so e.g.
    // shadow_dexee's finger meshes are oriented correctly.
    matrix
      .multiply(mujocoMeshRefMatrix(meshAsset))
      .multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
  } else if (geomType === 'cylinder' || geomType === 'capsule') {
    matrix.multiply(new THREE.Matrix4().makeRotationX(Math.PI / 2));
  }
  applyMatrixToObject(object, matrix);
}

async function parseMJCFWithMeshes(xmlText, filename, baseDir = '') {
  const expanded = await expandMujocoIncludes(xmlText, baseDir);
  const doc = parseXMLDocument(expanded);
  const root = doc.documentElement;
  if (root.localName !== 'mujoco') {
    throw new Error(`Expected MJCF <mujoco> root, got <${root.localName || 'unknown'}>.`);
  }

  // Honor <compiler angle="..." eulerseq="..."> for all orientation specifiers.
  const compilerEl = firstChildElement(root, 'compiler');
  const angleAttr = (compilerEl?.getAttribute('angle') || 'degree').toLowerCase();
  mjcfPoseCtx = {
    toRad: angleAttr === 'radian' ? 1 : Math.PI / 180,
    eulerseq: compilerEl?.getAttribute('eulerseq') || 'xyz'
  };

  const meshAssets = collectMujocoAssets(root);
  const defaults = collectMujocoDefaults(root);
  const group = new THREE.Group();
  group.name = root.getAttribute('model') || filename.replace(/\.[^.]+$/, '') || 'mujoco_scene';
  group.links = {};
  group.joints = {};
  group.setJointValue = (jointName, value) => {
    const joint = group.joints[jointName];
    if (!joint?.pivot) return;
    // MuJoCo joint coordinate is qpos; the geometric displacement relative to
    // the XML rest configuration is (qpos - ref). At qpos == ref the body is in
    // its authored pose (zero displacement).
    const disp = value - (joint.refRad || 0);
    if (joint.jointType === 'prismatic') {
      const offset = new THREE.Vector3(...joint.axis)
        .normalize()
        .applyQuaternion(joint.originQuat)
        .multiplyScalar(disp);
      joint.pivot.position.copy(joint.origin).add(offset);
    } else {
      joint.pivot.quaternion.copy(joint.originQuat)
        .multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(...joint.axis).normalize(), disp));
    }
  };

  const links = [];
  const joints = [];
  let visualCount = 0;
  let collisionCount = 0;

  async function visitBody(
    bodyNode,
    parentObject,
    parentName = '',
    parentWorldMatrix = new THREE.Matrix4(),
    inheritedChildClass = ''
  ) {
    const bodyAttrs = attrsFromElement(bodyNode);
    const childClass = bodyAttrs.childclass || inheritedChildClass;
    const bodyLocalMatrix = matrixFromPoseAttrs(bodyAttrs);
    const bodyWorldMatrix = new THREE.Matrix4().copy(parentWorldMatrix).multiply(bodyLocalMatrix);
    const linkName = bodyAttrs.name || `body_${links.length}`;
    const linkPayload = {
      name: linkName,
      inertial: parseMujocoInertial(bodyNode),
      visuals: [],
      collisions: []
    };

    const pivot = new THREE.Group();
    pivot.name = `${linkName}_joint`;
    applyMatrixToObject(pivot, bodyLocalMatrix);
    parentObject.add(pivot);

    const linkObject = new THREE.Group();
    linkObject.name = linkName;
    pivot.add(linkObject);
    group.links[linkName] = linkObject;

    if (parentName) {
      const jointNode = firstChildElement(bodyNode, 'joint');
      const jointAttrs = jointNode ? resolveMujocoAttrs(jointNode, defaults, 'joint', childClass) : {};
      const axis = parseNumbers(jointAttrs.axis, [0, 0, 1]);
      const range = parseNumbers(jointAttrs.range, []);
      const jointName = jointAttrs.name || `${parentName}_to_${linkName}${jointNode ? '' : '_fixed'}`;
      const rawType = jointNode ? (jointAttrs.type || 'hinge') : 'fixed';
      const type = jointNode ? mujocoJointType(jointAttrs.type || 'hinge') : 'fixed';
      // Per-joint DOF count and reference offset for keyframe (qpos) mapping.
      // ball=4 (quat), free=7, hinge/slide=1, fixed/weld=0. `ref` is the qpos at
      // which the joint matches its XML configuration; rotation = qpos - ref.
      const dofCount = rawType === 'ball' ? 4 : rawType === 'free' ? 7 : rawType === 'fixed' ? 0 : 1;
      const refVal = numberAttr(jointAttrs, 'ref');
      const refRad = Number.isFinite(refVal)
        ? (rawType === 'slide' ? refVal : refVal * mjcfPoseCtx.toRad)
        : 0;
      const bodyPose = decomposeMatrix(bodyLocalMatrix);
      const jointPos = parseNumbers(jointAttrs.pos, [0, 0, 0]);
      const localPos0 = transformPointArray(bodyLocalMatrix, jointPos);
      const localPos1 = jointPos;
      const localRot0 = quaternionToUSDArray(bodyPose.quaternion);
      const localRot1 = [1, 0, 0, 0];
      const jointInfo = {
        name: jointName,
        type,
        rawType,
        dofCount,
        refRad,
        parent: parentName,
        child: linkName,
        axis,
        axisToken: axisToToken(axis),
        origin: localPos0,
        originMatrix: matrixToUSDArray(matrixFromPoseAttrs(bodyAttrs)),
        localPos0,
        localPos1,
        localRot0,
        localRot1,
        // Range in joint-coordinate units (radians for hinge). MuJoCo `range` is
        // in the model's angle unit, so convert degrees -> radians; slide is
        // meters. The USD converter expects revolute limits in radians.
        limit: range.length >= 2
          ? (rawType === 'slide'
              ? { lower: range[0], upper: range[1] }
              : { lower: range[0] * mjcfPoseCtx.toRad, upper: range[1] * mjcfPoseCtx.toRad })
          : {},
        uiLimit: range.length >= 2
          ? (rawType === 'slide'
              ? { lower: range[0], upper: range[1] }
              : { lower: range[0] * mjcfPoseCtx.toRad, upper: range[1] * mjcfPoseCtx.toRad })
          : null,
        dynamics: {
          damping: numberAttr(jointAttrs, 'damping'),
          friction: numberAttr(jointAttrs, 'frictionloss')
        }
      };
      joints.push(jointInfo);
      group.joints[jointName] = {
        ...jointInfo,
        jointType: type,
        pivot,
        origin: pivot.position.clone(),
        originQuat: pivot.quaternion.clone()
      };
    }

    let geomIndex = 0;
    for (const geomNode of childElements(bodyNode, 'geom')) {
      const geomAttrs = resolveMujocoAttrs(geomNode, defaults, 'geom', childClass);
      const geomName = geomAttrs.name || geomAttrs.mesh || `${linkName}_geom_${geomIndex}`;
      const isVisual = classifyMujocoGeom(geomAttrs);
      let payloads = null;
      if (!isVisual) {
        payloads = shapePayloadForMujocoGeom(
          geomAttrs,
          new THREE.Matrix4().copy(bodyWorldMatrix).multiply(matrixFromPoseAttrs(geomAttrs)),
          geomName
        );
      }
      if (!payloads) payloads = await mujocoGeomPayloads(geomAttrs, meshAssets, geomName, bodyWorldMatrix);

      const object = await loadMujocoGeomObject(geomAttrs, meshAssets, geomName);
      object.name = geomName;
      applyMujocoObjectDisplayTransform(object, geomAttrs, meshAssets);
      linkObject.add(object);
      object.traverse((obj) => {
        if (!obj.isMesh) return;
        obj.userData.urdfOwnerLink = linkObject;
        obj.userData.urdfOwnerLinkName = linkName;
        obj.userData.urdfCollision = !isVisual;
        if (isVisual) {
          state.visualMeshes.push(obj);
        } else {
          obj.userData.originalMaterial = obj.material;
          obj.material = collisionMaterial;
          state.collisionMeshes.push(obj);
        }
      });

      if (isVisual) {
        linkPayload.visuals.push(...payloads);
        visualCount += payloads.length;
      } else {
        // Default approximation `convexHull` matches the writer in
        // `src/tydra/urdf-to-usd.cc::AddCollisionAPIs` and the
        // mujoco-usd-converter convention. Per-geom overrides via
        // payload.approximation are preserved.
        for (const payload of payloads) {
          payload.approximation = payload.approximation || 'convexHull';
        }
        linkPayload.collisions.push(...payloads);
        collisionCount += payloads.length;
      }
      geomIndex++;
    }

    links.push(linkPayload);

    for (const childBody of childElements(bodyNode, 'body')) {
      await visitBody(childBody, linkObject, linkName, bodyWorldMatrix, childClass);
    }
  }

  for (const worldbody of childElements(root, 'worldbody')) {
    for (const bodyNode of childElements(worldbody, 'body')) {
      await visitBody(bodyNode, group);
    }
  }

  // Parse the model's "home" keyframe (qpos) so the robot can be posed in its
  // natural configuration instead of the MuJoCo zero-pose. qpos layout: a
  // floating base contributes 7 leading values (pos3 + quat4), then one value
  // per single-DOF joint in body-tree (DFS) order — matching how `joints` is
  // built above. qpos angles are radians regardless of <compiler angle>.
  const keyEl = root.querySelector('keyframe key');
  if (keyEl?.getAttribute('qpos')) {
    const hasFree = !!root.querySelector('worldbody freejoint, worldbody joint[type="free"]');
    group.homeKeyframe = {
      qpos: parseNumbers(keyEl.getAttribute('qpos'), []),
      baseDofs: hasFree ? 7 : 0,
      // Joints in body-tree (DFS) order = MuJoCo qpos order. Each descriptor
      // carries its DOF count so multi-DOF joints (ball=4) advance the cursor
      // correctly and don't shift every subsequent joint's value.
      jointOrder: joints.map((j) => ({
        name: j.name, rawType: j.rawType, dofCount: j.dofCount, refRad: j.refRad
      }))
    };
  }

  state.exportPayload = {
    name: group.name,
    upAxis: state.settings.upAxis,
    gravity: state.settings.upAxis === 'Z' ? [0, 0, -1] : [0, -1, 0],
    timestep: numberAttr(firstChildElement(root, 'option'), 'timestep'),
    links,
    joints
  };
  group.userData.stats = { links: links.length, joints: joints.length, visuals: visualCount, collisions: collisionCount };
  return group;
}

// Pose a parsed MJCF robot to the MuJoCo default qpos0 = ref (XML rest pose).
// Slider value shows ref; geometric displacement (qpos - ref) is zero.
function poseToRest(robot) {
  if (!robot?.joints) return;
  for (const [name, joint] of Object.entries(robot.joints)) {
    const ref = joint.refRad || 0;
    state.jointValues[name] = ref;
    if (joint.rawType === 'ball') {
      if (joint.pivot && joint.originQuat) joint.pivot.quaternion.copy(joint.originQuat);
    } else if (typeof robot.setJointValue === 'function') {
      robot.setJointValue(name, ref);   // displacement = ref - ref = 0
    }
  }
}

// Pose to the model's "home" keyframe. qpos angles are radians; setJointValue
// applies the (qpos - ref) displacement, so the slider value shown is the raw
// qpos coordinate (matching the MuJoCo viewer). DOF-count aware: ball=4 (quat),
// free=7, hinge/slide=1, so multi-DOF joints don't shift later joints' values.
function poseFromKeyframe(robot) {
  const kf = robot?.homeKeyframe;
  if (!kf) { poseToRest(robot); return; }
  const qpos = kf.qpos;
  let cursor = kf.baseDofs;   // skip the floating-base DOFs (pos3 + quat4)
  for (const j of kf.jointOrder) {
    const dof = j.dofCount;
    if (dof === 0) continue;                 // fixed/welded body: no qpos
    if (cursor + dof > qpos.length) break;
    if (j.rawType === 'ball') {
      const jc = robot.joints[j.name];
      if (jc?.pivot) {
        const bq = new THREE.Quaternion(qpos[cursor + 1], qpos[cursor + 2], qpos[cursor + 3], qpos[cursor]).normalize();
        jc.pivot.quaternion.copy(jc.originQuat).multiply(bq);
      }
    } else if (j.rawType !== 'free' && typeof robot.setJointValue === 'function') {
      const qv = qpos[cursor];               // joint coordinate (qpos)
      state.jointValues[j.name] = qv;
      robot.setJointValue(j.name, qv);        // displacement = qv - ref
    }
    cursor += dof;
  }
}

// Apply the active pose preset (home keyframe vs. qpos0=ref) and resync views.
function applyPosePreset() {
  const robot = state.robot;
  if (!robot?.joints) return;
  if (state.settings.applyHomePose && robot.homeKeyframe) poseFromKeyframe(robot);
  else poseToRest(robot);
  rebuildJointControls();
  syncConvertedUSDToSourcePose();
  syncGhosts();
}

function classifyRobotMeshes(robot) {
  state.visualMeshes = [];
  state.collisionMeshes = [];

  const linkEntries = Object.entries(robot.links || {});
  const linkSet = new Set(linkEntries.map(([, link]) => link));
  const linkNames = new Map(linkEntries.map(([name, link]) => [link, name]));
  robot.traverse((obj) => {
    if (!obj.isMesh) return;
    let cursor = obj;
    let ownerLink = null;
    let isCollision = false;
    while (cursor && cursor !== robot) {
      const marker = `${cursor.type || ''} ${cursor.name || ''}`.toLowerCase();
      if (cursor.isURDFCollider || cursor.isURDFCollision || marker.includes('collision') || marker.includes('collider')) {
        isCollision = true;
      }
      if (linkSet.has(cursor)) {
        ownerLink = cursor;
        break;
      }
      cursor = cursor.parent;
    }
    obj.userData.urdfOwnerLink = ownerLink;
    obj.userData.urdfOwnerLinkName = ownerLink ? linkNames.get(ownerLink) : '';
    obj.userData.urdfCollision = isCollision;
    if (isCollision) {
      obj.userData.originalMaterial = obj.material;
      obj.material = collisionMaterial;
      state.collisionMeshes.push(obj);
    } else {
      state.visualMeshes.push(obj);
    }
  });

  applyVisibility();
}

function applyVisibility() {
  for (const mesh of state.visualMeshes) mesh.visible = state.settings.showVisuals;
  for (const mesh of state.collisionMeshes) mesh.visible = state.settings.showCollisions;
  applyRenderableVisibility(state.usdObject);
  applyRenderableVisibility(state.usdGhost);
  applyRenderableVisibility(state.sourceGhost);
}

function currentFitObjects() {
  return [state.robot, state.usdObject].filter(Boolean);
}

function applyRenderableVisibility(root) {
  root?.traverse((obj) => {
    if (!obj.isMesh) return;
    obj.visible = isCollisionObject(obj)
      ? state.settings.showCollisions
      : state.settings.showVisuals;
  });
}

function fitCamera(rootOrObjects) {
  const objects = Array.isArray(rootOrObjects) ? rootOrObjects : [rootOrObjects].filter(Boolean);
  const box = new THREE.Box3();
  for (const object of objects) {
    if (object) { object.updateWorldMatrix(true, true); box.expandByObject(object); }
  }
  if (box.isEmpty()) return;
  const size = box.getSize(new THREE.Vector3());
  const center = box.getCenter(new THREE.Vector3());
  const radius = Math.max(size.length() * 0.5, 1e-3);
  // Distance so the bounding sphere fits BOTH the vertical and horizontal
  // frustum of a single split view (each view is ~half the window width, so the
  // horizontal FOV is the tighter constraint), plus a safety margin.
  const fovV = (camera.fov * Math.PI) / 180;
  const aspect = camera.aspect || 1;
  const fovH = 2 * Math.atan(Math.tan(fovV / 2) * aspect);
  const dist = (radius / Math.sin(Math.max(Math.min(fovV, fovH) / 2, 1e-3))) * 1.15;
  const dir = new THREE.Vector3(1.1, 0.75, 1.35).normalize();
  controls.target.copy(center);
  camera.position.copy(center).add(dir.multiplyScalar(dist));
  camera.near = Math.max(dist / 1000, 1e-4);
  camera.far = dist * 100;
  camera.updateProjectionMatrix();
  controls.update();
}

function fitCurrentView() {
  fitCamera(currentFitObjects());
}

function applySceneOrientation() {
  for (const root of [sourceView.root, sourceView.ghostRoot, usdView.root, usdView.ghostRoot]) {
    root.rotation.set(0, 0, 0);
  }
  if (state.settings.upAxis === 'Z') {
    for (const root of [sourceView.root, sourceView.ghostRoot, usdView.root, usdView.ghostRoot]) {
      root.rotation.x = -Math.PI / 2;
    }
  }
}

function applyAxisHelperVisibility() {
  sourceView.axisHelper.visible = state.settings.showAxisHelper;
  usdView.axisHelper.visible = state.settings.showAxisHelper;
}

function buildGUI() {
  const gui = new GUI({ title: 'Robot Controls' });
  gui.add(state.settings, 'upAxis', ['Z', 'Y']).name('Export upAxis').onChange(() => {
    applySceneOrientation();
    if ((state.robot || state.usdObject) && state.settings.autocenter) fitCamera(currentFitObjects());
  });
  gui.add(state.settings, 'showVisuals').name('Visual meshes').onChange(applyVisibility);
  gui.add(state.settings, 'showCollisions').name('Collision meshes').onChange(applyVisibility);
  gui.add(state.settings, 'ignoreJointLimits').name('Ignore limits').onChange(rebuildJointControls);
  gui.add(state.settings, 'hideFixedJoints').name('Hide fixed joints').onChange(rebuildJointControls);
  gui.add(state.settings, 'applyHomePose').name('Home pose').onChange(() => {
    applyPosePreset();
    if (state.robot && state.settings.autocenter) fitCamera(currentFitObjects());
  });
  gui.add(state.settings, 'animateJoints').name('Animate joints');
  gui.add(state.settings, 'animationSpeed', 0.1, 4.0, 0.1).name('Animation speed');
  gui.add(state.settings, 'autocenter').name('Autocenter');
  gui.add(state.settings, 'showAxisHelper').name('Axis helper').onChange(applyAxisHelperVisibility);
  gui.add(state.settings, 'showLinkLines').name('Link lines').onChange(updateSkeletonDebugVisualizations);
  gui.add(state.settings, 'showJointArrows').name('Joint arrows').onChange(updateSkeletonDebugVisualizations);
  gui.add(state.settings, 'showLinkNames').name('Link names').onChange(updateSkeletonDebugVisualizations);
  gui.add(state.settings, 'showJointNames').name('Joint names').onChange(updateSkeletonDebugVisualizations);
  gui.add(state.settings, 'ghostUSDInSource').name('USD ghost on left').onChange(rebuildGhosts);
  gui.add(state.settings, 'ghostSourceInUSD').name('Source ghost on right').onChange(rebuildGhosts);
  gui.add(state.settings, 'ghostOpacity', 0.05, 0.75, 0.01).name('Ghost opacity').onChange(() => {
    setGhostOpacity(state.usdGhost, state.settings.ghostOpacity);
    setGhostOpacity(state.sourceGhost, state.settings.ghostOpacity);
  });
}

function setJointValue(jointName, value) {
  const joint = state.joints[jointName];
  if (!joint) return;
  state.jointValues[jointName] = value;
  const controls = state.jointControls.get(jointName);
  if (controls) {
    controls.number.value = value.toFixed(3);
    controls.range.value = String(value);
  }
  if (state.robot?.setJointValue) {
    state.robot.setJointValue(jointName, value);
  } else if (joint.setJointValue) {
    joint.setJointValue(value);
  }
  if (state.usdArticulation?.setJointValue && state.usdArticulation !== state.robot) {
    state.usdArticulation.setJointValue(jointName, value);
  }
  syncConvertedUSDToSourcePose();
  syncGhosts();
}

function jointLimits(joint) {
  const type = joint.jointType || joint.type || 'fixed';
  const limit = joint.uiLimit || joint.limit || {};
  if (type === 'continuous') return [-Math.PI, Math.PI, 0.01];
  if (type === 'prismatic') {
    return state.settings.ignoreJointLimits
      ? [-1, 1, 0.001]
      : [Number.isFinite(limit.lower) ? limit.lower : -1, Number.isFinite(limit.upper) ? limit.upper : 1, 0.001];
  }
  return state.settings.ignoreJointLimits
    ? [-Math.PI, Math.PI, 0.01]
    : [Number.isFinite(limit.lower) ? limit.lower : -Math.PI, Number.isFinite(limit.upper) ? limit.upper : Math.PI, 0.01];
}

function rebuildJointControls() {
  jointControlsEl.innerHTML = '';
  state.jointControls.clear();
  const jointSource = state.robot || state.usdArticulation;
  if (!jointSource) return;
  state.joints = jointSource.joints || {};
  const jointEntries = Object.entries(state.joints);
  for (const [name, joint] of jointEntries) {
    const type = joint.jointType || joint.type || 'fixed';
    if (state.settings.hideFixedJoints && type === 'fixed') continue;

    const [min, max, step] = jointLimits(joint);
    const value = state.jointValues[name] ?? 0;
    const row = document.createElement('div');
    row.className = 'joint-row';
    row.innerHTML = `
      <div>
        <div class="joint-name" title="${escapeXML(name)}">${escapeXML(name)}</div>
        <div class="joint-type">${escapeXML(type)}</div>
      </div>
      <input type="number" min="${min}" max="${max}" step="${step}" value="${value.toFixed(3)}">
      <input type="range" min="${min}" max="${max}" step="${step}" value="${value}">
    `;
    const number = row.querySelector('input[type="number"]');
    const range = row.querySelector('input[type="range"]');
    const onInput = (event) => {
      const next = Number(event.target.value);
      if (!Number.isFinite(next)) return;
      number.value = next.toFixed(3);
      range.value = String(next);
      setJointValue(name, next);
    };
    number.addEventListener('input', onInput);
    range.addEventListener('input', onInput);
    state.jointControls.set(name, { number, range });
    jointControlsEl.appendChild(row);
  }
}

function updateRobotInfo(metadata) {
  document.getElementById('robotName').textContent = metadata.name || state.robot?.name || '-';
  document.getElementById('linkCount').textContent = String(metadata.links?.size || Object.keys(state.robot?.links || state.usdArticulation?.links || {}).length);
  document.getElementById('jointCount').textContent = String(metadata.joints?.length || Object.keys(state.robot?.joints || state.usdArticulation?.joints || {}).length);
  document.getElementById('meshCount').textContent = String(metadata.meshes ?? (state.visualMeshes.length + state.collisionMeshes.length));
}

function updateLabels() {
  const sourceName = state.inputName || 'URDF/MJCF';
  const sourceFormat = state.inputFormat ? state.inputFormat.toUpperCase() : 'URDF/MJCF';
  sourceLabelEl.textContent = `${sourceFormat}: ${sourceName}`;
  usdLabelEl.textContent = state.usdName ? `USD: ${state.usdName}` : 'USD';
}

function updateJointPanelFold() {
  panelEl.classList.toggle('joints-collapsed', state.settings.jointsCollapsed);
  jointHeaderEl.setAttribute('aria-expanded', String(!state.settings.jointsCollapsed));
  jointFoldIconEl.textContent = state.settings.jointsCollapsed ? '▸' : '▾';
}

function toggleJointPanelFold() {
  state.settings.jointsCollapsed = !state.settings.jointsCollapsed;
  updateJointPanelFold();
}

async function loadRobotFile(file) {
  clearRobot();
  rememberAssetFile(file);
  state.inputText = await file.text();
  state.inputName = file.name;
  state.inputFormat = detectInputFormat(state.inputText);
  setStatus(`Parsing ${state.inputFormat.toUpperCase()} ${file.name}...`);
  const baseDir = dirname(file.webkitRelativePath || file.name);
  const robot = state.inputFormat === 'mjcf'
    ? await parseMJCFWithMeshes(state.inputText, file.name, baseDir)
    : await parseURDFWithMeshes(state.inputText, file.name);
  state.robot = robot;
  robotGroup.add(robot);
  applySceneOrientation();
  if (state.inputFormat === 'urdf') classifyRobotMeshes(robot);
  else applyVisibility();
  rebuildJointControls();
  const metadata = state.inputFormat === 'mjcf'
    ? {
      name: robot.name,
      links: new Map(Object.keys(robot.links || {}).map((name) => [name, { name }])),
      joints: Object.values(robot.joints || {})
    }
    : parseURDFMetadata(state.inputText);
  updateRobotInfo(metadata);
  captureSourceRestLinkMatrices();
  // Apply the active pose preset: MuJoCo default qpos0 = ref (the viewer's load
  // state), or the home keyframe when the "Home pose" toggle is on. Rest capture
  // above stays at the XML rest, keeping the USD binding reference clean.
  if (state.inputFormat === 'mjcf') applyPosePreset();
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
  if (state.settings.autocenter) fitCamera(currentFitObjects());
  setStatus(`Loaded ${state.inputFormat.toUpperCase()} ${file.name}`);
}

function matrixToUSDArray(matrix) {
  const e = matrix.elements;
  return [
    e[0], e[1], e[2], e[3],
    e[4], e[5], e[6], e[7],
    e[8], e[9], e[10], e[11],
    e[12], e[13], e[14], e[15]
  ];
}

function geometryPayload(mesh, ownerLink) {
  const geom = mesh.geometry;
  const pos = geom.getAttribute('position');
  if (!pos || pos.count < 3) return null;

  const normal = geom.getAttribute('normal');
  const uv = geom.getAttribute('uv');
  const index = geom.getIndex();
  const rel = new THREE.Matrix4()
    .copy(ownerLink.matrixWorld)
    .invert()
    .multiply(mesh.matrixWorld);

  return {
    name: mesh.name || mesh.parent?.name || 'mesh',
    matrix: matrixToUSDArray(rel),
    geometry: {
      positions: Array.from(pos.array),
      normals: normal ? Array.from(normal.array) : [],
      uvs: uv ? Array.from(uv.array) : [],
      indices: index ? Array.from(index.array) : []
    }
  };
}

function buildExportPayload() {
  if (!state.robot) throw new Error('No URDF robot loaded.');
  state.robot.updateWorldMatrix(true, true);
  const metadata = parseURDFMetadata(state.inputText);

  const linkPayloads = new Map();
  for (const [name, link] of Object.entries(state.robot.links || {})) {
    const info = metadata.links.get(name) || { name, inertial: {} };
    linkPayloads.set(name, {
      name,
      inertial: info.inertial || {},
      visuals: [],
      collisions: []
    });
    link.updateWorldMatrix(true, true);
  }

  for (const mesh of [...state.visualMeshes, ...state.collisionMeshes]) {
    const ownerLink = mesh.userData.urdfOwnerLink;
    if (!ownerLink) continue;
    const linkName = mesh.userData.urdfOwnerLinkName || ownerLink.name;
    const linkPayload = linkPayloads.get(linkName);
    if (!linkPayload) continue;
    const payload = geometryPayload(mesh, ownerLink);
    if (!payload) continue;
    if (mesh.userData.urdfCollision) {
      // Match the URDF→USD writer convention (convexHull) so re-export
      // round-trips don't silently regress to triangle-soup collisions.
      payload.approximation = payload.approximation || 'convexHull';
      linkPayload.collisions.push(payload);
    } else {
      linkPayload.visuals.push(payload);
    }
  }

  return {
    name: metadata.name || state.robot.name || 'Robot',
    upAxis: state.settings.upAxis,
    gravity: state.settings.upAxis === 'Z' ? [0, 0, -1] : [0, -1, 0],
    links: Array.from(linkPayloads.values()),
    joints: metadata.joints
  };
}

function buildCurrentExportPayload() {
  if (state.inputFormat === 'mjcf') {
    if (!state.exportPayload) throw new Error('No MJCF export payload is available.');
    return {
      ...state.exportPayload,
      upAxis: state.settings.upAxis,
      gravity: state.settings.upAxis === 'Z' ? [0, 0, -1] : [0, -1, 0]
    };
  }
  return buildExportPayload();
}

const DEG_TO_RAD = Math.PI / 180;

function basenameFromPath(primPath) {
  return String(primPath || '').split('/').filter(Boolean).pop() || '';
}

function parentLinkPath(primPath, linkPaths) {
  let best = null;
  for (const linkPath of linkPaths) {
    if (primPath === linkPath || primPath.startsWith(`${linkPath}/`)) {
      if (!best || linkPath.length > best.length) best = linkPath;
    }
  }
  return best;
}

function hasApi(prim, apiName) {
  return (prim.apiSchemas || []).some((api) => api === apiName || api.startsWith(`${apiName}:`));
}

function firstRelTarget(prim, name) {
  const rel = prim.relationships?.[name];
  if (Array.isArray(rel)) return rel[0] || '';
  return '';
}

function matrixTranslation(matrix) {
  if (!Array.isArray(matrix) || matrix.length < 15) return [0, 0, 0];
  return [matrix[12] || 0, matrix[13] || 0, matrix[14] || 0];
}

function matrixScale(matrix) {
  if (!Array.isArray(matrix) || matrix.length < 11) return [1, 1, 1];
  return [
    Math.hypot(matrix[0] || 0, matrix[1] || 0, matrix[2] || 0) || 1,
    Math.hypot(matrix[4] || 0, matrix[5] || 0, matrix[6] || 0) || 1,
    Math.hypot(matrix[8] || 0, matrix[9] || 0, matrix[10] || 0) || 1
  ];
}

function geometryToUrdf(geometry = {}, matrix) {
  if (geometry.type === 'box' || geometry.type === 'cube') {
    const scale = matrixScale(matrix);
    // GeomCube emits a scalar size (USD schema); legacy form emits a vec3.
    const base = Array.isArray(geometry.size)
      ? geometry.size
      : (typeof geometry.size === 'number'
          ? [geometry.size, geometry.size, geometry.size]
          : [2, 2, 2]);
    return { type: 'box', size: base.map((v, i) => Number(v || 1) * scale[i]) };
  }
  if (geometry.type === 'sphere') return { type: 'sphere', radius: Number(geometry.radius || 0) };
  if (geometry.type === 'cylinder' || geometry.type === 'capsule') {
    return {
      type: geometry.type,
      radius: Number(geometry.radius || 0),
      length: Number(geometry.length || geometry.height || 0)
    };
  }
  if (geometry.type === 'plane') {
    return { type: 'box', size: [Number(geometry.width || 1), Number(geometry.length || 1), 0.001] };
  }
  return { type: 'mesh', filename: `extracted:${geometry.source || 'mesh'}` };
}

function usdPhysicsToUrdf(extracted) {
  const prims = extracted.prims || [];
  const linkPrims = prims.filter((prim) => hasApi(prim, 'PhysicsRigidBodyAPI'));
  const linkPaths = new Set(linkPrims.map((prim) => prim.path));
  const links = linkPrims.map((prim) => ({
    name: prim.name || basenameFromPath(prim.path),
    path: prim.path,
    inertial: {
      mass: Number(prim.properties?.['physics:mass'] || 0),
      centerOfMass: prim.properties?.['physics:centerOfMass'] || [0, 0, 0],
      diagonalInertia: prim.properties?.['physics:diagonalInertia'] || [0, 0, 0]
    },
    visuals: [],
    collisions: []
  }));
  const linksByPath = new Map(links.map((link) => [link.path, link]));

  for (const prim of prims) {
    if (!prim.geometry) continue;
    const linkPath = parentLinkPath(prim.path, linkPaths);
    if (!linkPath) continue;
    const link = linksByPath.get(linkPath);
    const item = {
      name: prim.name || basenameFromPath(prim.path),
      origin: matrixTranslation(prim.matrix),
      geometry: geometryToUrdf(prim.geometry, prim.matrix)
    };
    if (hasApi(prim, 'PhysicsCollisionAPI')) link.collisions.push(item);
    else link.visuals.push(item);
  }

  const joints = prims
    .filter((prim) => /^Physics(?:Revolute|Prismatic|Fixed|Joint)/.test(prim.type))
    .map((prim) => {
      const type = prim.type === 'PhysicsRevoluteJoint'
        ? 'revolute'
        : prim.type === 'PhysicsPrismaticJoint'
          ? 'prismatic'
          : 'fixed';
      const parentPath = firstRelTarget(prim, 'physics:body0');
      const childPath = firstRelTarget(prim, 'physics:body1');
      const axisToken = prim.properties?.['physics:axis'] || 'X';
      const lower = prim.properties?.['physics:lowerLimit'];
      const upper = prim.properties?.['physics:upperLimit'];
      return {
        name: prim.name || basenameFromPath(prim.path),
        type,
        parent: linksByPath.get(parentPath)?.name || basenameFromPath(parentPath),
        child: linksByPath.get(childPath)?.name || basenameFromPath(childPath),
        origin: prim.properties?.['physics:localPos0'] || [0, 0, 0],
        axis: axisToken === 'Y' ? [0, 1, 0] : axisToken === 'Z' ? [0, 0, 1] : [1, 0, 0],
        limit: lower !== undefined || upper !== undefined
          ? {
            lower: type === 'revolute' ? Number(lower || 0) * DEG_TO_RAD : Number(lower || 0),
            upper: type === 'revolute' ? Number(upper || 0) * DEG_TO_RAD : Number(upper || 0)
          }
          : null,
        dynamics: jointDynamicsFromUsdPrim(prim, type)
      };
    });

  return {
    name: extracted.name || 'ConvertedFromUSDPhysics',
    upAxis: extracted.upAxis || 'Y',
    links,
    joints
  };
}

// Collect joint dynamics from a USD prim, accepting either the
// legacy MJC namespace (mjc:*) or the PhysX/Newton equivalents
// (physxJoint:*, physxLimit:*) — the first authored source wins.
// Mirrors ref/newton/newton/_src/usd/schemas.py SchemaResolverPhysx +
// ref/genesis/genesis/options/morphs.py revolute_joint_*_attr_candidates.
function jointDynamicsFromUsdPrim(prim, jointType) {
  const props = prim.properties || {};
  const limitNs = jointType === 'prismatic'
    ? 'physxLimit:linear:' : 'physxLimit:angular:';
  function pickNumber(...keys) {
    for (const k of keys) {
      const v = props[k];
      if (typeof v === 'number' && Number.isFinite(v)) return v;
    }
    return undefined;
  }
  return {
    damping: pickNumber('mjc:damping', limitNs + 'damping'),
    friction: pickNumber('mjc:frictionloss',
                          'physxJoint:jointFriction'),
    stiffness: pickNumber('mjc:stiffness', limitNs + 'stiffness'),
    armature: pickNumber('mjc:armature', 'physxJoint:armature')
  };
}

function usdPhysicsToSourceModel(extracted) {
  const prims = extracted.prims || [];
  const linkPrims = prims.filter((prim) => hasApi(prim, 'PhysicsRigidBodyAPI'));
  const links = linkPrims.map((prim) => ({
    name: prim.name || basenameFromPath(prim.path),
    path: prim.path,
    inertial: {
      mass: Number(prim.properties?.['physics:mass'] || 0),
      centerOfMass: prim.properties?.['physics:centerOfMass'] || [0, 0, 0],
      diagonalInertia: prim.properties?.['physics:diagonalInertia'] || [0, 0, 0]
    }
  }));
  const linksByPath = new Map(links.map((link) => [link.path, link]));

  const joints = prims
    .filter((prim) => /^Physics(?:Revolute|Prismatic|Fixed|Joint)/.test(prim.type))
    .map((prim) => {
      const type = prim.type === 'PhysicsRevoluteJoint'
        ? 'revolute'
        : prim.type === 'PhysicsPrismaticJoint'
          ? 'prismatic'
          : 'fixed';
      const parentPath = firstRelTarget(prim, 'physics:body0');
      const childPath = firstRelTarget(prim, 'physics:body1');
      const axisToken = prim.properties?.['physics:axis'] || 'X';
      const lower = prim.properties?.['physics:lowerLimit'];
      const upper = prim.properties?.['physics:upperLimit'];
      return {
        name: prim.name || basenameFromPath(prim.path),
        type,
        parent: linksByPath.get(parentPath)?.name || basenameFromPath(parentPath),
        child: linksByPath.get(childPath)?.name || basenameFromPath(childPath),
        parentPath,
        childPath,
        axis: axisToken === 'Y' ? [0, 1, 0] : axisToken === 'Z' ? [0, 0, 1] : [1, 0, 0],
        axisToken,
        origin: prim.properties?.['physics:localPos0'] || [0, 0, 0],
        localPos0: prim.properties?.['physics:localPos0'],
        localPos1: prim.properties?.['physics:localPos1'],
        localRot0: prim.properties?.['physics:localRot0'],
        localRot1: prim.properties?.['physics:localRot1'],
        limit: lower !== undefined || upper !== undefined
          ? {
            lower: type === 'revolute' ? Number(lower || 0) * DEG_TO_RAD : Number(lower || 0),
            upper: type === 'revolute' ? Number(upper || 0) * DEG_TO_RAD : Number(upper || 0)
          }
          : {},
        dynamics: jointDynamicsFromUsdPrim(prim, type)
      };
    });

  // Three independent collider signals; see `usdGeometryClassification`
  // above for rationale and ordering.
  const collisionPaths = new Set(
    prims
      .filter((prim) => prim.geometry
          && (hasApi(prim, 'PhysicsCollisionAPI')
              || Number(prim.properties?.['mjc:group']) >= 3
              || prim.purpose === 'guide'))
      .map((prim) => prim.path)
  );

  return {
    name: extracted.name || basenameFromPath(state.usdName) || 'ConvertedFromUSDPhysics',
    upAxis: extracted.upAxis || 'Y',
    links,
    joints,
    collisionPaths
  };
}

function fmtNumber(value) {
  if (!Number.isFinite(value)) return '0';
  return Number(value).toPrecision(9).replace(/\.?0+$/u, '');
}

function vec(value, fallback = [0, 0, 0]) {
  const a = Array.isArray(value) ? value : fallback;
  return [a[0] || 0, a[1] || 0, a[2] || 0].map(fmtNumber).join(' ');
}

function quatAttr(quaternion) {
  if (!quaternion) return '';
  const q = quaternion.clone().normalize();
  if (Math.abs(q.x) < 1e-9 && Math.abs(q.y) < 1e-9 && Math.abs(q.z) < 1e-9 && Math.abs(q.w - 1) < 1e-9) return '';
  return ` quat="${fmtNumber(q.w)} ${fmtNumber(q.x)} ${fmtNumber(q.y)} ${fmtNumber(q.z)}"`;
}

function mjcfBodyPoseFromJoint(joint) {
  const bodyMatrix = jointFrameMatrix(joint, 0).multiply(jointFrameMatrix(joint, 1).invert());
  return decomposeMatrix(bodyMatrix);
}

function mjcfJointPosAttr(joint) {
  const pos = joint.localPos1 || [0, 0, 0];
  return vectorFromArray(pos).lengthSq() > 1e-18 ? ` pos="${vec(pos)}"` : '';
}

function escapeXML(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function geometryXML(geometry = {}) {
  if (geometry.type === 'box' || geometry.type === 'cube') {
    // GeomCube emits a scalar size (USD schema); legacy form emits a vec3.
    const size = Array.isArray(geometry.size)
      ? geometry.size
      : (typeof geometry.size === 'number'
          ? [geometry.size, geometry.size, geometry.size]
          : [1, 1, 1]);
    return `<box size="${vec(size, [1, 1, 1])}"/>`;
  }
  if (geometry.type === 'sphere') return `<sphere radius="${fmtNumber(geometry.radius || 0)}"/>`;
  if (geometry.type === 'cylinder') {
    return `<cylinder radius="${fmtNumber(geometry.radius || 0)}" length="${fmtNumber(geometry.length || 0)}"/>`;
  }
  return `<mesh filename="${escapeXML(geometry.filename || 'extracted:mesh.obj')}"/>`;
}

function urdfToXML(robot) {
  const lines = [`<robot name="${escapeXML(robot.name)}">`];
  for (const link of robot.links) {
    lines.push(`  <link name="${escapeXML(link.name)}">`);
    if (link.inertial && link.inertial.mass > 0) {
      const i = link.inertial.diagonalInertia || [0, 0, 0];
      lines.push('    <inertial>');
      lines.push(`      <origin xyz="${vec(link.inertial.centerOfMass)}" rpy="0 0 0"/>`);
      lines.push(`      <mass value="${fmtNumber(link.inertial.mass)}"/>`);
      lines.push(`      <inertia ixx="${fmtNumber(i[0] || 0)}" ixy="0" ixz="0" iyy="${fmtNumber(i[1] || 0)}" iyz="0" izz="${fmtNumber(i[2] || 0)}"/>`);
      lines.push('    </inertial>');
    }
    for (const visual of link.visuals) {
      lines.push(`    <visual name="${escapeXML(visual.name)}">`);
      lines.push(`      <origin xyz="${vec(visual.origin)}" rpy="0 0 0"/>`);
      lines.push(`      <geometry>${geometryXML(visual.geometry)}</geometry>`);
      lines.push('    </visual>');
    }
    for (const collision of link.collisions) {
      lines.push(`    <collision name="${escapeXML(collision.name)}">`);
      lines.push(`      <origin xyz="${vec(collision.origin)}" rpy="0 0 0"/>`);
      lines.push(`      <geometry>${geometryXML(collision.geometry)}</geometry>`);
      lines.push('    </collision>');
    }
    lines.push('  </link>');
  }
  for (const joint of robot.joints) {
    lines.push(`  <joint name="${escapeXML(joint.name)}" type="${joint.type}">`);
    lines.push(`    <parent link="${escapeXML(joint.parent)}"/>`);
    lines.push(`    <child link="${escapeXML(joint.child)}"/>`);
    lines.push(`    <origin xyz="${vec(joint.origin)}" rpy="0 0 0"/>`);
    lines.push(`    <axis xyz="${vec(joint.axis, [1, 0, 0])}"/>`);
    if (joint.limit) {
      lines.push(`    <limit lower="${fmtNumber(joint.limit.lower)}" upper="${fmtNumber(joint.limit.upper)}" effort="0" velocity="0"/>`);
    }
    if (joint.dynamics?.damping !== undefined || joint.dynamics?.friction !== undefined) {
      lines.push(`    <dynamics damping="${fmtNumber(joint.dynamics.damping || 0)}" friction="${fmtNumber(joint.dynamics.friction || 0)}"/>`);
    }
    lines.push('  </joint>');
  }
  lines.push('</robot>');
  return `${lines.join('\n')}\n`;
}

function mjcfFromSourceModel(model) {
  const childJoints = new Map();
  const childLinks = new Set();
  for (const joint of model.joints || []) {
    childLinks.add(joint.child);
    if (!childJoints.has(joint.parent)) childJoints.set(joint.parent, []);
    childJoints.get(joint.parent).push(joint);
  }

  const linksByName = new Map((model.links || []).map((link) => [link.name, link]));
  const roots = (model.links || []).filter((link) => !childLinks.has(link.name));
  const axis = (value) => vec(value, [1, 0, 0]);

  const lines = [
    `<mujoco model="${escapeXML(model.name || 'ConvertedFromUSD')}">`,
    '  <worldbody>'
  ];

  function writeBody(link, indent) {
    const pad = ' '.repeat(indent);
    lines.push(`${pad}<body name="${escapeXML(link.name)}">`);
    for (const joint of childJoints.get(link.name) || []) {
      const child = linksByName.get(joint.child);
      if (!child) continue;
      const jointType = joint.type === 'prismatic' ? 'slide' : joint.type === 'fixed' ? 'fixed' : 'hinge';
      const range = joint.limit && Number.isFinite(joint.limit.lower) && Number.isFinite(joint.limit.upper)
        ? ` range="${fmtNumber(joint.limit.lower)} ${fmtNumber(joint.limit.upper)}"`
        : '';
      const bodyPose = mjcfBodyPoseFromJoint(joint);
      lines.push(`${pad}  <body name="${escapeXML(child.name)}" pos="${vec(bodyPose.position.toArray())}"${quatAttr(bodyPose.quaternion)}>`);
      if (jointType !== 'fixed') {
        lines.push(`${pad}    <joint name="${escapeXML(joint.name)}" type="${jointType}" axis="${axis(joint.axis)}"${mjcfJointPosAttr(joint)}${range}/>`);
      }
      lines.push(`${pad}    <!-- Mesh geometry is displayed from the loaded USD scene in the web demo. -->`);
      for (const grandChild of childJoints.get(child.name) || []) {
        const grandLink = linksByName.get(grandChild.child);
        if (grandLink) writeBodyFromJoint(child, grandChild, grandLink, indent + 4);
      }
      lines.push(`${pad}  </body>`);
    }
    lines.push(`${pad}</body>`);
  }

  function writeBodyFromJoint(parent, joint, link, indent) {
    const pad = ' '.repeat(indent);
    const jointType = joint.type === 'prismatic' ? 'slide' : joint.type === 'fixed' ? 'fixed' : 'hinge';
    const range = joint.limit && Number.isFinite(joint.limit.lower) && Number.isFinite(joint.limit.upper)
      ? ` range="${fmtNumber(joint.limit.lower)} ${fmtNumber(joint.limit.upper)}"`
      : '';
    const bodyPose = mjcfBodyPoseFromJoint(joint);
    lines.push(`${pad}<body name="${escapeXML(link.name)}" pos="${vec(bodyPose.position.toArray())}"${quatAttr(bodyPose.quaternion)}>`);
    if (jointType !== 'fixed') {
      lines.push(`${pad}  <joint name="${escapeXML(joint.name)}" type="${jointType}" axis="${axis(joint.axis)}"${mjcfJointPosAttr(joint)}${range}/>`);
    }
    lines.push(`${pad}  <!-- Mesh geometry is displayed from the loaded USD scene in the web demo. -->`);
    for (const childJoint of childJoints.get(link.name) || []) {
      const childLink = linksByName.get(childJoint.child);
      if (childLink) writeBodyFromJoint(link, childJoint, childLink, indent + 2);
    }
    lines.push(`${pad}</body>`);
  }

  for (const root of roots) {
    writeBody(root, 4);
  }
  lines.push('  </worldbody>');
  lines.push('</mujoco>');
  return `${lines.join('\n')}\n`;
}

function findObjectsByUSDPath(root) {
  const byPath = new Map();
  root?.traverse((obj) => {
    const path = usdPathForObject(obj);
    if (path && !byPath.has(path)) byPath.set(path, obj);
  });
  return byPath;
}

function usdPathForObject(obj) {
  let cursor = obj;
  while (cursor) {
    const path = cursor.userData?.['primMeta.absPath'];
    if (path) {
      if (cursor === obj) return path;
      const name = sanitizeUSDIdentifier(obj.name || 'mesh', 'mesh');
      return `${path}/${name}`;
    }
    cursor = cursor.parent;
  }
  return '';
}

function bestOwnerLinkPath(path, linkPaths) {
  let best = '';
  for (const linkPath of linkPaths) {
    if (path === linkPath || path.startsWith(`${linkPath}/`)) {
      if (linkPath.length > best.length) best = linkPath;
    }
  }
  return best;
}

function cloneMeshForSource(mesh, ownerLink, linkName, isCollision) {
  const clone = new THREE.Mesh();
  clone.name = mesh.name;
  clone.renderOrder = mesh.renderOrder;
  if (mesh.geometry) clone.geometry = mesh.geometry.clone();
  if (Array.isArray(mesh.material)) {
    clone.material = mesh.material.map((mat) => mat.clone?.() || mat);
  } else if (mesh.material) {
    clone.material = mesh.material.clone?.() || mesh.material;
  }
  clone.userData = {
    ...mesh.userData,
    urdfOwnerLink: ownerLink,
    urdfOwnerLinkName: linkName,
    urdfCollision: isCollision
  };
  if (isCollision) {
    clone.userData.originalMaterial = clone.material;
    clone.material = collisionMaterial.clone();
  }
  return clone;
}

function matrixInSceneRoot(object, sceneRoot) {
  object.updateWorldMatrix(true, false);
  const parent = sceneRoot?.parent;
  if (!parent) return object.matrixWorld.clone();
  parent.updateWorldMatrix(true, false);
  return parent.matrixWorld.clone().invert().multiply(object.matrixWorld);
}

function matrixInUSDScene(object) {
  return matrixInSceneRoot(object, state.usdObject);
}

function vectorFromArray(values, fallback = [0, 0, 0]) {
  const v = Array.isArray(values) ? values : fallback;
  return new THREE.Vector3(v[0] || 0, v[1] || 0, v[2] || 0);
}

function quaternionFromUSDValue(value) {
  if (Array.isArray(value)) {
    if (value.length >= 4) return new THREE.Quaternion(value[1] || 0, value[2] || 0, value[3] || 0, value[0] ?? 1).normalize();
    if (value.length === 3) return new THREE.Quaternion(value[0] || 0, value[1] || 0, value[2] || 0, 1).normalize();
  }
  if (value && typeof value === 'object') {
    return new THREE.Quaternion(value.x || 0, value.y || 0, value.z || 0, value.w ?? value.real ?? 1).normalize();
  }
  return new THREE.Quaternion();
}

function jointFrameMatrix(joint, side) {
  const suffix = side === 1 ? '1' : '0';
  const posFallback = side === 1 ? [0, 0, 0] : joint?.origin;
  return new THREE.Matrix4().compose(
    vectorFromArray(joint?.[`localPos${suffix}`], posFallback),
    quaternionFromUSDValue(joint?.[`localRot${suffix}`]),
    new THREE.Vector3(1, 1, 1)
  );
}

function buildRestWorldByLinkPath(model, usdObjectsByPath, parentJointByChild, childJointsByParent, sceneRoot) {
  const restWorldByLinkPath = new Map();
  const rootLinks = (model.links || []).filter((link) => !parentJointByChild.has(link.path));
  const visit = (linkPath, restWorld) => {
    restWorldByLinkPath.set(linkPath, restWorld.clone());
    for (const joint of childJointsByParent.get(linkPath) || []) {
      const parentJointFrame = jointFrameMatrix(joint, 0);
      const childJointFrameInverse = jointFrameMatrix(joint, 1).invert();
      const childRest = restWorld.clone().multiply(parentJointFrame).multiply(childJointFrameInverse);
      visit(joint.childPath, childRest);
    }
  };

  for (const link of rootLinks) {
    const object = usdObjectsByPath.get(link.path);
    const rootRest = object ? matrixInSceneRoot(object, sceneRoot) : new THREE.Matrix4();
    visit(link.path, rootRest);
  }

  for (const link of model.links || []) {
    if (!restWorldByLinkPath.has(link.path)) {
      const object = usdObjectsByPath.get(link.path);
      restWorldByLinkPath.set(link.path, object ? matrixInSceneRoot(object, sceneRoot) : new THREE.Matrix4());
    }
  }

  return restWorldByLinkPath;
}

function meshRestWorldByLinkPath(sourceObject, linkPaths, collisionPaths) {
  const restByLinkPath = new Map();
  sourceObject?.updateWorldMatrix(true, true);
  sourceObject?.traverse((obj) => {
    if (!obj.isMesh) return;
    const path = usdPathForObject(obj);
    if (pathInSetOrUnder(path, collisionPaths)) return;
    const linkPath = bestOwnerLinkPath(path, linkPaths);
    if (!linkPath || restByLinkPath.has(linkPath)) return;
    restByLinkPath.set(linkPath, matrixInSceneRoot(obj, sourceObject));
  });
  return restByLinkPath;
}

function jointHasAuthoredRotations(joint) {
  return Array.isArray(joint?.localRot0) && Array.isArray(joint?.localRot1);
}

function modelNeedsJointFrameInference(model) {
  return (model.joints || []).some((joint) => !jointHasAuthoredRotations(joint));
}

function inferMissingJointFrames(model, restWorldByLinkPath) {
  for (const joint of model.joints || []) {
    if (jointHasAuthoredRotations(joint)) continue;
    const parentRest = restWorldByLinkPath.get(joint.parentPath);
    const childRest = restWorldByLinkPath.get(joint.childPath);
    if (!parentRest || !childRest) continue;
    const relative = parentRest.clone().invert().multiply(childRest);
    if (!Array.isArray(joint.localPos0)) {
      const { position } = decomposeMatrix(relative);
      joint.origin = position.toArray();
      joint.localPos0 = position.toArray();
    }
    if (!Array.isArray(joint.localPos1)) joint.localPos1 = [0, 0, 0];

    const local0Translation = new THREE.Matrix4().makeTranslation(...joint.localPos0);
    const local1Translation = new THREE.Matrix4().makeTranslation(...joint.localPos1);
    const rotationOnly = local0Translation.clone().invert().multiply(relative).multiply(local1Translation);
    const { quaternion } = decomposeMatrix(rotationOnly);
    joint.localRot0 = quaternionToUSDArray(quaternion);
    joint.localRot1 = [1, 0, 0, 0];
  }
}

function buildArticulatedRobotFromUSD(model, sourceObject, options = {}) {
  if (!sourceObject) throw new Error('No USD view is loaded.');
  const group = new THREE.Group();
  group.name = options.name || `${model.name || usdBaseName()}_mjcf`;
  group.links = {};
  group.joints = {};

  const usdObjectsByPath = findObjectsByUSDPath(sourceObject);
  const linkPaths = new Set((model.links || []).map((link) => link.path));
  const linksByPath = new Map((model.links || []).map((link) => [link.path, link]));
  const parentJointByChild = new Map((model.joints || []).map((joint) => [joint.childPath, joint]));
  const childJointsByParent = new Map();
  for (const joint of model.joints || []) {
    if (!childJointsByParent.has(joint.parentPath)) childJointsByParent.set(joint.parentPath, []);
    childJointsByParent.get(joint.parentPath).push(joint);
  }

  sourceObject.updateWorldMatrix(true, true);
  const restWorldByLinkPath = buildRestWorldByLinkPath(
    model,
    usdObjectsByPath,
    parentJointByChild,
    childJointsByParent,
    sourceObject
  );
  if (options.inferMissingJointFrames && modelNeedsJointFrameInference(model)) {
    const meshRestFrames = meshRestWorldByLinkPath(sourceObject, linkPaths, model.collisionPaths);
    for (const [path, matrix] of meshRestFrames) {
      restWorldByLinkPath.set(path, matrix.clone());
    }
    inferMissingJointFrames(model, restWorldByLinkPath);
  }

  const nodeByLinkPath = new Map();
  for (const link of model.links || []) {
    const pivot = new THREE.Group();
    pivot.name = `${link.name}_joint`;
    const linkObject = new THREE.Group();
    linkObject.name = link.name;
    linkObject.userData['primMeta.absPath'] = link.path;
    pivot.add(linkObject);
    group.links[link.name] = linkObject;
    nodeByLinkPath.set(link.path, { link, pivot, linkObject });
  }

  const rootLinks = (model.links || []).filter((link) => !parentJointByChild.has(link.path));
  const attachLink = (linkPath, parentObject = group) => {
    const node = nodeByLinkPath.get(linkPath);
    if (!node) return;
    const { link, pivot, linkObject } = node;
    const joint = parentJointByChild.get(linkPath);
    if (joint) {
      applyMatrixToObject(pivot, jointFrameMatrix(joint, 0));
      applyMatrixToObject(linkObject, jointFrameMatrix(joint, 1).invert());
    } else {
      const rootRest = restWorldByLinkPath.get(linkPath) || new THREE.Matrix4();
      applyMatrixToObject(pivot, rootRest);
      linkObject.position.set(0, 0, 0);
      linkObject.quaternion.identity();
      linkObject.scale.set(1, 1, 1);
    }
    parentObject.add(pivot);

    if (joint) {
      group.joints[joint.name] = {
        ...joint,
        jointType: joint.type,
        pivot,
        origin: pivot.position.clone(),
        originQuat: pivot.quaternion.clone()
      };
    }

    for (const childJoint of childJointsByParent.get(linkPath) || []) {
      attachLink(childJoint.childPath, linkObject);
    }
  };
  for (const link of rootLinks) attachLink(link.path, group);

  const linkInverseWorld = new Map();
  for (const [path, restWorld] of restWorldByLinkPath) {
    linkInverseWorld.set(path, restWorld.clone().invert());
  }

  if (options.resetMeshLists) {
    state.visualMeshes = [];
    state.collisionMeshes = [];
  }
  sourceObject.traverse((obj) => {
    if (!obj.isMesh) return;
    const path = usdPathForObject(obj);
    const linkPath = bestOwnerLinkPath(path, linkPaths);
    if (!linkPath) return;
    const node = nodeByLinkPath.get(linkPath);
    const link = linksByPath.get(linkPath);
    if (!node || !link) return;
    const isCollision = pathInSetOrUnder(path, model.collisionPaths) || isCollisionObject(obj);
    const clone = cloneMeshForSource(obj, node.linkObject, link.name, isCollision);
    const local = linkInverseWorld.get(linkPath).clone().multiply(matrixInSceneRoot(obj, sourceObject));
    applyMatrixToObject(clone, local);
    node.linkObject.add(clone);
    if (options.resetMeshLists) {
      if (isCollision) state.collisionMeshes.push(clone);
      else state.visualMeshes.push(clone);
    }
  });

  group.setJointValue = (jointName, value) => {
    const joint = group.joints[jointName];
    if (!joint?.pivot) return;
    if (joint.jointType === 'prismatic') {
      const offset = axisVector(joint.axis).normalize().applyQuaternion(joint.originQuat).multiplyScalar(value);
      joint.pivot.position.copy(joint.origin).add(offset);
    } else if (joint.jointType !== 'fixed') {
      joint.pivot.quaternion.copy(joint.originQuat)
        .multiply(new THREE.Quaternion().setFromAxisAngle(axisVector(joint.axis).normalize(), value));
    }
  };

  return group;
}

function buildGeneratedSourceFromUSD(model) {
  return buildArticulatedRobotFromUSD(model, state.usdRestObject || state.usdObject, {
    name: `${model.name || usdBaseName()}_mjcf`,
    resetMeshLists: true,
    inferMissingJointFrames: true
  });
}

async function ensureNativeExporter() {
  const loader = await ensureTinyLoader();
  if (!state.nativeExporter) {
    state.nativeExporter = new loader.native_.TinyUSDZLoaderNative();
    const { maxUsdcMb, maxMemMb } = resolveUSDCExportCapsFromRuntime();
    // Raise the USDC writer's conservative WASM size caps so mesh-dense scenes
    // (e.g. robot_soccer_kit ~104MB, apptronik_apollo ~111MB) can export past
    // the 100MB default.
    state.nativeExporter.setUSDCExportLimitMB?.(maxUsdcMb, maxMemMb);
  }
  return state.nativeExporter;
}

function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

function sourceBaseName() {
  return `${state.inputName || state.robot?.name || 'robot'}`
    .replace(/\.(urdf|xml)$/i, '')
    .replace(/[^A-Za-z0-9_.-]+/g, '_');
}

function usdBaseName() {
  const name = state.usdName || state.exportPayload?.name || state.robot?.name || 'robot';
  return `${name}`.replace(/\.(usd|usda|usdc|usdz)$/i, '').replace(/[^A-Za-z0-9_.-]+/g, '_');
}

function bytesFromNativeView(view) {
  if (!view) return null;
  return new Uint8Array(view);
}

function convertedUSDPreviewMaterial() {
  return new THREE.MeshStandardMaterial({
    color: 0x9aa5ad,
    roughness: 0.62,
    metalness: 0.0
  });
}

function shapeGeometryFromPayload(shape = {}) {
  if (shape.type === 'box') return new THREE.BoxGeometry(2, 2, 2);
  if (shape.type === 'sphere') return new THREE.SphereGeometry(shape.radius || 0.5, 24, 12);
  if (shape.type === 'cylinder') {
    const geometry = new THREE.CylinderGeometry(shape.radius || 0.5, shape.radius || 0.5, shape.height || 1, 24, 1);
    geometry.rotateX(Math.PI / 2);
    return geometry;
  }
  if (shape.type === 'capsule') {
    const radius = shape.radius || 0.5;
    const height = Math.max(shape.height || 1, 0.001);
    const geometry = typeof THREE.CapsuleGeometry === 'function'
      ? new THREE.CapsuleGeometry(radius, height, 12, 24)
      : new THREE.CylinderGeometry(radius, radius, height, 24, 1);
    geometry.rotateX(Math.PI / 2);
    return geometry;
  }
  if (shape.type === 'plane') {
    return new THREE.PlaneGeometry(shape.width || 1, shape.length || 1);
  }
  return null;
}

function matrixFromUSDArray(values) {
  const matrix = new THREE.Matrix4();
  if (Array.isArray(values) && values.length === 16) {
    matrix.fromArray(values);
  }
  return matrix;
}

function geometryFromPayloadItem(item, geometryCache) {
  const meshRef = item?.meshRef;
  if (meshRef && geometryCache.has(meshRef)) return geometryCache.get(meshRef);

  if (item?.shape) {
    const key = `shape:${JSON.stringify(item.shape)}`;
    if (geometryCache.has(key)) return geometryCache.get(key);
    const geometry = shapeGeometryFromPayload(item.shape);
    if (geometry) {
      geometry.computeBoundingSphere();
      geometryCache.set(key, geometry);
    }
    return geometry;
  }

  const buffer = meshRef ? state.nativeMeshBuffers.get(meshRef) : item?.geometry;
  const positions = buffer?.positions;
  if (!positions || positions.length < 9) return null;

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute('position', new THREE.BufferAttribute(asFloat32Array(positions), 3));
  if (buffer.normals?.length === positions.length) {
    geometry.setAttribute('normal', new THREE.BufferAttribute(asFloat32Array(buffer.normals), 3));
  } else {
    geometry.computeVertexNormals();
  }
  if (buffer.uvs?.length === (positions.length / 3) * 2) {
    geometry.setAttribute('uv', new THREE.BufferAttribute(asFloat32Array(buffer.uvs), 2));
  }
  if (buffer.indices?.length) {
    geometry.setIndex(new THREE.BufferAttribute(asUint32Array(buffer.indices), 1));
  }
  geometry.computeBoundingSphere();
  if (meshRef) geometryCache.set(meshRef, geometry);
  return geometry;
}

function addPayloadMeshesToLink(linkGroup, items, geometryCache, material, isCollision, inverseSourceRest) {
  for (const item of items || []) {
    const geometry = geometryFromPayloadItem(item, geometryCache);
    if (!geometry) continue;
    const mesh = new THREE.Mesh(geometry, material.clone());
    mesh.name = sanitizeUSDIdentifier(item.name || (isCollision ? 'collision' : 'visual'), isCollision ? 'collision' : 'visual');
    const linkPath = linkGroup.userData?.['primMeta.absPath'] || `/World/Links/${linkGroup.name}`;
    mesh.userData['primMeta.absPath'] = `${linkPath}/${mesh.name}`;
    mesh.userData.urdfCollision = isCollision;
    let matrix = matrixFromUSDArray(item.matrix);
    if (state.inputFormat === 'mjcf' && inverseSourceRest) {
      matrix = new THREE.Matrix4().copy(inverseSourceRest).multiply(matrix);
    }
    applyMatrixToObject(mesh, matrix);
    if (isCollision) mesh.renderOrder = 8;
    linkGroup.add(mesh);
  }
}

function buildConvertedUSDPreviewFromPayload(payload) {
  const root = new THREE.Group();
  root.name = usdBaseName();
  const linksScope = new THREE.Group();
  linksScope.name = 'Links';
  root.add(linksScope);

  const geometryCache = new Map();
  const visualMaterial = convertedUSDPreviewMaterial();

  const sourceRestByLink = state.sourceRestLinkMatrices;
  for (const link of payload.links || []) {
    const linkName = link.name || 'link';
    const linkGroup = new THREE.Group();
    linkGroup.name = sanitizeUSDIdentifier(linkName, 'link');
    linkGroup.userData['primMeta.absPath'] = `/World/Links/${linkGroup.name}`;

    const sourceRest = sourceRestByLink.get(linkName);
    const inverseSourceRest = sourceRest ? sourceRest.clone().invert() : null;
    if (sourceRest) applyMatrixToObject(linkGroup, sourceRest);

    addPayloadMeshesToLink(linkGroup, link.visuals, geometryCache, visualMaterial, false, inverseSourceRest);
    addPayloadMeshesToLink(linkGroup, link.collisions, geometryCache, collisionMaterial, true, inverseSourceRest);

    linksScope.add(linkGroup);
  }

  return root;
}

function registerNativeMeshBuffer(native, item, collision) {
  const buffer = state.nativeMeshBuffers.get(item.meshRef);
  if (!buffer) throw new Error(`Missing native mesh buffer for ${item.meshRef}`);
  const fn = collision ? native.setCollisionMesh : native.setVisualMesh;
  if (typeof fn !== 'function') {
    throw new Error('TinyUSDZ WASM does not expose setVisualMesh/setCollisionMesh.');
  }
  const ok = fn.call(native, item.meshRef, buffer.positions, buffer.normals, buffer.uvs, buffer.indices);
  if (!ok) throw new Error(native.error() || `Failed to register mesh buffer ${item.meshRef}`);
}

function registerNativeMeshBuffers(native, payload) {
  const used = new Set();
  const register = (item, collision) => {
    if (!item?.meshRef || used.has(item.meshRef)) return;
    registerNativeMeshBuffer(native, item, collision);
    used.add(item.meshRef);
  };

  native.clearURDFMeshBuffers?.();
  for (const link of payload.links || []) {
    for (const visual of link.visuals || []) register(visual, false);
    for (const collision of link.collisions || []) register(collision, true);
  }
}

function exportNativeStage(native, format, payload = null) {
  if (format === 'usda') {
    const text = native.exportAsUSDA();
    if (!text) throw new Error(native.error());
    return {
      bytes: new TextEncoder().encode(text),
      blob: new Blob([text], { type: 'text/plain' }),
      filename: `${usdBaseName()}.usda`,
      payload
    };
  }
  if (format === 'usdc') {
    const bytes = bytesFromNativeView(native.exportAsUSDC());
    if (!bytes) throw new Error(native.error());
    return {
      bytes,
      blob: new Blob([bytes], { type: 'application/octet-stream' }),
      filename: `${usdBaseName()}.usdc`,
      payload
    };
  }
  const bytes = bytesFromNativeView(native.exportAsUSDZ());
  if (!bytes) throw new Error(native.error());
  return {
    bytes,
    blob: new Blob([bytes], { type: 'model/vnd.usdz+zip' }),
    filename: `${usdBaseName()}.usdz`,
    payload
  };
}

async function createUSDBytesFromSource(format) {
  const native = await ensureNativeExporter();
  const payload = buildCurrentExportPayload();
  registerNativeMeshBuffers(native, payload);
  setStatus(`Creating USD Physics + MuJoCo stage for ${payload.name}...`);
  const ok = native.createURDFPhysicsScene(JSON.stringify(payload));
  if (!ok) throw new Error(native.error());
  return exportNativeStage(native, format, payload);
}

async function createUSDBytesFromImportedUSD(format) {
  const native = await ensureNativeExporter();
  if (!state.latestUSDBytes) throw new Error('No imported or converted USD is loaded.');
  if (!native.loadFromBinary(state.latestUSDBytes, state.usdName || `scene.${state.latestUSDFormat || 'usd'}`)) {
    throw new Error(native.error() || 'Failed to load current USD for export.');
  }
  return exportNativeStage(native, format);
}

async function createUSDBytes(format) {
  return state.robot ? createUSDBytesFromSource(format) : createUSDBytesFromImportedUSD(format);
}

function exportSourceXML() {
  if (!state.inputText) throw new Error('No URDF/MJCF source is loaded.');
  const ext = state.inputFormat === 'urdf' ? 'urdf' : 'xml';
  const type = state.inputFormat === 'urdf' ? 'application/xml' : 'text/xml';
  downloadBlob(new Blob([state.inputText], { type }), `${sourceBaseName()}.${ext}`);
  setStatus(`Exported ${state.inputFormat.toUpperCase()} XML.`);
}

async function convertSourceToUSD(format = 'usdc') {
  const result = await createUSDBytesFromSource(format);
  clearUSD();
  const restObject = await loadUSDObjectFromBytes(result.bytes, result.filename);
  const object = buildConvertedUSDPreviewFromPayload(result.payload);
  state.usdObject = object;
  state.usdRestObject = restObject;
  state.usdName = result.filename;
  state.latestUSDBytes = result.bytes;
  state.latestUSDFormat = format;
  usdGroup.add(object);
  applySceneOrientation();
  bindConvertedUSDLinksToSource();
  syncConvertedUSDToSourcePose();
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
  applyVisibility();
  if (state.settings.autocenter) fitCamera(currentFitObjects());
  setStatus(`Converted ${state.inputFormat.toUpperCase()} to ${format.toUpperCase()} for comparison.`);
}

async function convertUSDToSource() {
  if (!state.latestUSDBytes) throw new Error('No USD is loaded.');
  const native = await ensureNativeExporter();
  if (!native.loadFromBinary(state.latestUSDBytes, state.usdName || `scene.${state.latestUSDFormat || 'usd'}`)) {
    throw new Error(native.error() || 'Failed to load USD for conversion.');
  }
  const jsonText = native.extractPhysicsSceneJSON();
  if (!jsonText) throw new Error(native.error() || 'USD Physics extraction failed.');
  const sourceModel = usdPhysicsToSourceModel(JSON.parse(jsonText));
  clearRobot();
  const robot = buildGeneratedSourceFromUSD(sourceModel);
  state.exportPayload = {
    ...sourceModel,
    links: sourceModel.links.map((link) => ({
      ...link,
      visuals: link.visuals || [],
      collisions: link.collisions || []
    }))
  };
  state.inputText = mjcfFromSourceModel(sourceModel);
  state.inputName = `${usdBaseName()}_from_usd.xml`;
  state.inputFormat = 'mjcf';
  state.robot = robot;
  robotGroup.add(robot);
  applySceneOrientation();
  applyVisibility();
  rebuildJointControls();
  updateRobotInfo({
    name: sourceModel.name,
    links: new Map(sourceModel.links.map((link) => [link.name, link])),
    joints: sourceModel.joints
  });
  captureSourceRestLinkMatrices();
  bindConvertedUSDLinksToSource();
  syncConvertedUSDToSourcePose();
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
  if (state.settings.autocenter) fitCamera(currentFitObjects());
  setStatus(`Converted USD to generated MJCF view: ${sourceModel.links.length} links, ${sourceModel.joints.length} joints.`);
}

async function exportRobot(format) {
  try {
    const result = await createUSDBytes(format);
    downloadBlob(result.blob, result.filename);
    const native = await ensureNativeExporter();
    const warn = native.warn?.();
    setStatus(warn ? `Exported ${format.toUpperCase()} with warnings.` : `Exported ${format.toUpperCase()}.`);
    if (warn) console.warn(warn);
  } catch (err) {
    console.error(err);
    setStatus(`Export failed: ${err.message}`);
  }
}

document.getElementById('urdfInput').addEventListener('change', (event) => {
  const file = event.target.files?.[0];
  if (file) loadRobotFile(file).catch((err) => {
    console.error(err);
    setStatus(`Load failed: ${err.message}`);
  });
  event.target.value = '';
});

document.getElementById('assetInput').addEventListener('change', (event) => {
  rebuildAssetFiles(Array.from(event.target.files || []));
});

document.getElementById('usdInput').addEventListener('change', (event) => {
  const file = event.target.files?.[0];
  if (file) loadUSDFile(file).catch((err) => {
    console.error(err);
    setStatus(`USD load failed: ${err.message}`);
  });
  event.target.value = '';
});

sourceExportButton.addEventListener('click', () => {
  try {
    exportSourceXML();
  } catch (err) {
    console.error(err);
    setStatus(`Source export failed: ${err.message}`);
  }
});

convertToUSDButton.addEventListener('click', () => {
  convertSourceToUSD('usdc').catch((err) => {
    console.error(err);
    setStatus(`Convert to USD failed: ${err.message}`);
  });
});

convertToSourceButton.addEventListener('click', () => {
  convertUSDToSource().catch((err) => {
    console.error(err);
    setStatus(`Convert to MJCF failed: ${err.message}`);
  });
});

document.getElementById('exportUSDA').addEventListener('click', () => exportRobot('usda'));
document.getElementById('exportUSDC').addEventListener('click', () => exportRobot('usdc'));
document.getElementById('exportUSDZ').addEventListener('click', () => exportRobot('usdz'));
fitViewButton.addEventListener('click', fitCurrentView);
jointHeaderEl.addEventListener('click', toggleJointPanelFold);

// Collapse/expand the whole Robot/Joints panel (foldable header).
const panelHeaderEl = document.getElementById('panelHeader');
const togglePanelFold = () => panelEl.classList.toggle('collapsed');
panelHeaderEl?.addEventListener('click', togglePanelFold);
panelHeaderEl?.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); togglePanelFold(); }
});

// Diagnostics hook for headless drivers: count effectively-visible meshes per
// view so a "blank render" (model loaded but nothing actually drawn) can be
// detected instead of silently passing.
window.__fitView = () => fitCamera(currentFitObjects());
window.__viewerStats = () => {
  const effVisible = (o) => { for (let c = o; c; c = c.parent) { if (!c.visible) return false; } return true; };
  const countVisible = (root) => {
    let n = 0;
    root.traverse((o) => { if (o.isMesh && effVisible(o)) n++; });
    return n;
  };
  return {
    links: Object.keys(state.robot?.links || state.usdArticulation?.links || {}).length,
    sourceVisibleMeshes: countVisible(robotGroup),
    usdVisibleMeshes: countVisible(usdGroup)
  };
};

window.addEventListener('keydown', (event) => {
  if (event.key.toLowerCase() !== 'f' || event.altKey || event.ctrlKey || event.metaKey) return;
  const tagName = event.target?.tagName?.toLowerCase();
  if (tagName === 'input' || tagName === 'textarea' || tagName === 'select' || event.target?.isContentEditable) return;
  event.preventDefault();
  fitCurrentView();
});

window.addEventListener('resize', () => {
  camera.aspect = Math.max(window.innerWidth * 0.5, 1) / Math.max(window.innerHeight, 1);
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

const clock = new THREE.Clock();
function renderSplitView() {
  const width = renderer.domElement.clientWidth;
  const height = renderer.domElement.clientHeight;
  const leftWidth = Math.floor(width * 0.5);
  const rightWidth = width - leftWidth;

  camera.aspect = Math.max(leftWidth, 1) / Math.max(height, 1);
  camera.updateProjectionMatrix();
  renderer.setScissorTest(true);
  renderer.clear();

  renderer.setViewport(0, 0, leftWidth, height);
  renderer.setScissor(0, 0, leftWidth, height);
  renderer.render(sourceView.scene, camera);

  renderer.setViewport(leftWidth, 0, rightWidth, height);
  renderer.setScissor(leftWidth, 0, rightWidth, height);
  renderer.render(usdView.scene, camera);

  renderer.setScissorTest(false);
}

function animate() {
  requestAnimationFrame(animate);
  const elapsed = clock.getElapsedTime();
  if ((state.robot || state.usdArticulation) && state.settings.animateJoints) {
    for (const [name, joint] of Object.entries(state.joints)) {
      const type = joint.jointType || joint.type || 'fixed';
      if (type === 'fixed') continue;
      const [min, max] = jointLimits(joint);
      const mid = (min + max) * 0.5;
      const amp = (max - min) * 0.45;
      if (Number.isFinite(mid) && Number.isFinite(amp)) {
        setJointValue(name, mid + Math.sin(elapsed * state.settings.animationSpeed) * amp);
      }
    }
  }
  controls.update();
  syncConvertedUSDToSourcePose();
  syncGhosts();
  updateSkeletonDebugVisualizations();
  renderSplitView();
}

buildGUI();
updateButtonStates();
updateLabels();
updateJointPanelFold();
animate();
