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
  sourceGhost: null,
  usdGhost: null,
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
  tinyLoader: null,
  nativeModule: null,
  nativeExporter: null,
  joints: {},
  jointValues: {},
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
    ghostOpacity: 0.28
  }
};

const USD_MESH_EXTENSIONS = new Set(['.usd', '.usda', '.usdc', '.usdz']);

function createViewScene() {
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x202124);
  const root = new THREE.Group();
  const ghostRoot = new THREE.Group();
  scene.add(root);
  scene.add(ghostRoot);
  scene.add(new THREE.HemisphereLight(0xddeeff, 0x303030, 1.6));
  const dirLight = new THREE.DirectionalLight(0xffffff, 2.4);
  dirLight.position.set(4, 6, 3);
  dirLight.castShadow = true;
  scene.add(dirLight);
  scene.add(new THREE.GridHelper(20, 40, 0x586069, 0x33383d));
  return { scene, root, ghostRoot };
}

const sourceView = createViewScene();
const usdView = createViewScene();
const scene = sourceView.scene;

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
  color: 0xff8a3d,
  roughness: 0.75,
  metalness: 0.0,
  transparent: true,
  opacity: 0.28,
  wireframe: true,
  depthWrite: false
});

const statusEl = document.getElementById('status');
const jointControlsEl = document.getElementById('jointControls');
const sourceLabelEl = document.getElementById('sourceLabel');
const usdLabelEl = document.getElementById('usdLabel');
const sourceExportButton = document.getElementById('exportSource');
const convertToUSDButton = document.getElementById('convertToUSD');
const convertToSourceButton = document.getElementById('convertToSource');
const exportButtons = [
  document.getElementById('exportUSDA'),
  document.getElementById('exportUSDC'),
  document.getElementById('exportUSDZ')
];

function setStatus(text) {
  statusEl.textContent = text;
}

function setExportEnabled(enabled) {
  sourceExportButton.disabled = !enabled;
  convertToUSDButton.disabled = !enabled;
  for (const button of exportButtons) button.disabled = !(enabled || state.usdObject || state.latestUSDBytes);
}

function setUSDEnabled(enabled) {
  convertToSourceButton.disabled = !enabled;
  for (const button of exportButtons) button.disabled = !(enabled || state.robot);
}

function updateButtonStates() {
  const hasSource = Boolean(state.robot);
  const hasUSD = Boolean(state.usdObject || state.latestUSDBytes);
  sourceExportButton.disabled = !hasSource;
  convertToUSDButton.disabled = !hasSource;
  convertToSourceButton.disabled = !hasUSD;
  for (const button of exportButtons) button.disabled = !(hasSource || hasUSD);
}

function disposeObject(root) {
  root.traverse((obj) => {
    if (obj.geometry) obj.geometry.dispose();
    if (obj.material && obj.material !== collisionMaterial) {
      const mats = Array.isArray(obj.material) ? obj.material : [obj.material];
      for (const mat of mats) mat.dispose?.();
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

function clearRobot() {
  if (state.robot) {
    clearObjectFromGroup(robotGroup, state.robot);
  }
  state.robot = null;
  state.inputText = '';
  state.inputName = '';
  state.inputFormat = '';
  state.exportPayload = null;
  state.joints = {};
  state.jointValues = {};
  state.collisionMeshes = [];
  state.visualMeshes = [];
  jointControlsEl.innerHTML = '';
  updateButtonStates();
  clearGhosts();
  updateLabels();
}

function clearUSD() {
  clearObjectFromGroup(usdGroup, state.usdObject);
  state.usdObject = null;
  state.usdName = '';
  state.latestUSDBytes = null;
  state.latestUSDFormat = '';
  updateButtonStates();
  clearGhosts();
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
  const object = TinyUSDZLoaderUtils.buildThreeNode(rootNode, defaultMaterial, sceneData, { overrideMaterial: false });
  object.name = filename.replace(/\.[^.]+$/, '') || 'usd_scene';
  return object;
}

async function loadUSDFile(file) {
  clearUSD();
  const bytes = new Uint8Array(await file.arrayBuffer());
  const object = await loadUSDObjectFromBytes(bytes, file.name);
  state.usdObject = object;
  state.usdName = file.name;
  state.latestUSDBytes = bytes;
  state.latestUSDFormat = extension(file.name).slice(1) || 'usd';
  usdGroup.add(object);
  applySceneOrientation();
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
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
  const clone = source.clone(true);
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
      originMatrix: originToUSDMatrix(origin, parseNumbers(jointEl.querySelector(':scope > origin')?.getAttribute('rpy'), [0, 0, 0])),
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

function matrixFromPoseAttrs(attrs = {}) {
  const pos = parseNumbers(attrs.pos, [0, 0, 0]);
  const translation = new THREE.Vector3(pos[0] || 0, pos[1] || 0, pos[2] || 0);
  let quat = new THREE.Quaternion();

  if (attrs.quat) {
    const q = parseNumbers(attrs.quat, [1, 0, 0, 0]);
    quat = new THREE.Quaternion(q[1] || 0, q[2] || 0, q[3] || 0, q[0] ?? 1).normalize();
  } else if (attrs.euler) {
    const e = parseNumbers(attrs.euler, [0, 0, 0]);
    quat = new THREE.Quaternion().setFromEuler(new THREE.Euler(e[0] || 0, e[1] || 0, e[2] || 0, 'XYZ'));
  }

  return new THREE.Matrix4().compose(translation, quat, new THREE.Vector3(1, 1, 1));
}

function applyMatrixToObject(object, matrix) {
  matrix.decompose(object.position, object.quaternion, object.scale);
}

function collectMujocoAssets(root) {
  const compiler = firstChildElement(root, 'compiler');
  const meshDir = compiler?.getAttribute('meshdir') || '';
  const meshes = new Map();
  for (const asset of childElements(root, 'asset')) {
    for (const mesh of childElements(asset, 'mesh')) {
      const file = mesh.getAttribute('file') || '';
      if (!file) continue;
      const name = mesh.getAttribute('name') || file.split('/').pop().replace(/\.[^.]+$/, '');
      meshes.set(name, {
        path: joinPath(meshDir, file),
        scale: parseNumbers(mesh.getAttribute('scale'), [1, 1, 1])
      });
    }
  }
  return meshes;
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

function shapePayloadForMujocoGeom(geomNode, originMatrix, fallbackName) {
  const attrs = attrsFromElement(geomNode);
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
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: {
        type: geomType,
        radius: size[0] || 0.5,
        height: size[1] ? size[1] * 2 : 1,
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

async function loadMujocoGeomObject(geomNode, meshAssets, fallbackName) {
  const attrs = attrsFromElement(geomNode);
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    if (!meshAsset) return makeMissingMesh(attrs.mesh || fallbackName);
    return loadMeshObject(meshAsset.path);
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
    const length = size[1] ? size[1] * 2 : 1;
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

async function mujocoGeomPayloads(geomNode, meshAssets, fallbackName) {
  const attrs = attrsFromElement(geomNode);
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  const originMatrix = matrixFromPoseAttrs(attrs);
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    if (!meshAsset) return [];
    const object = await loadMeshObject(meshAsset.path);
    const scale = parseNumbers(attrs.scale, meshAsset.scale || [1, 1, 1]);
    const meshMatrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
    return collectMeshPayloads(object, meshMatrix, fallbackName);
  }

  const object = await loadMujocoGeomObject(geomNode, meshAssets, fallbackName);
  const yToZ = geomType === 'cylinder' || geomType === 'capsule'
    ? new THREE.Matrix4().makeRotationX(Math.PI / 2)
    : new THREE.Matrix4();
  return collectMeshPayloads(object, new THREE.Matrix4().copy(originMatrix).multiply(yToZ), fallbackName);
}

function classifyMujocoGeom(geomNode) {
  const attrs = attrsFromElement(geomNode);
  return attrs.class === 'visual' ||
    attrs.group === '2' ||
    (attrs.contype === '0' && attrs.conaffinity === '0');
}

function applyMujocoObjectDisplayTransform(object, geomNode, meshAssets) {
  const attrs = attrsFromElement(geomNode);
  const geomType = attrs.type || (attrs.mesh ? 'mesh' : 'sphere');
  const matrix = matrixFromPoseAttrs(attrs);
  if (geomType === 'mesh') {
    const meshAsset = meshAssets.get(attrs.mesh);
    const scale = parseNumbers(attrs.scale, meshAsset?.scale || [1, 1, 1]);
    matrix.multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
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

  const meshAssets = collectMujocoAssets(root);
  const group = new THREE.Group();
  group.name = root.getAttribute('model') || filename.replace(/\.[^.]+$/, '') || 'mujoco_scene';
  group.links = {};
  group.joints = {};
  group.setJointValue = (jointName, value) => {
    const joint = group.joints[jointName];
    if (!joint?.pivot) return;
    if (joint.jointType === 'prismatic') {
      joint.pivot.position.copy(joint.origin).add(new THREE.Vector3(...joint.axis).multiplyScalar(value));
    } else {
      joint.pivot.quaternion.copy(joint.originQuat)
        .multiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(...joint.axis).normalize(), value));
    }
  };

  const links = [];
  const joints = [];
  let visualCount = 0;
  let collisionCount = 0;

  async function visitBody(bodyNode, parentObject, parentName = '') {
    const bodyAttrs = attrsFromElement(bodyNode);
    const linkName = bodyAttrs.name || `body_${links.length}`;
    const linkPayload = {
      name: linkName,
      inertial: parseMujocoInertial(bodyNode),
      visuals: [],
      collisions: []
    };

    const pivot = new THREE.Group();
    pivot.name = `${linkName}_joint`;
    applyMatrixToObject(pivot, matrixFromPoseAttrs(bodyAttrs));
    parentObject.add(pivot);

    const linkObject = new THREE.Group();
    linkObject.name = linkName;
    pivot.add(linkObject);
    group.links[linkName] = linkObject;

    if (parentName) {
      const jointNode = firstChildElement(bodyNode, 'joint');
      const jointAttrs = attrsFromElement(jointNode);
      const axis = parseNumbers(jointAttrs.axis, [0, 0, 1]);
      const range = parseNumbers(jointAttrs.range, []);
      const jointName = jointAttrs.name || `${parentName}_to_${linkName}${jointNode ? '' : '_fixed'}`;
      const type = jointNode ? mujocoJointType(jointAttrs.type || 'hinge') : 'fixed';
      const origin = parseNumbers(bodyAttrs.pos, [0, 0, 0]);
      const jointInfo = {
        name: jointName,
        type,
        parent: parentName,
        child: linkName,
        axis,
        axisToken: axisToToken(axis),
        origin,
        originMatrix: matrixToUSDArray(matrixFromPoseAttrs(bodyAttrs)),
        limit: range.length >= 2 ? { lower: range[0], upper: range[1] } : {},
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
      const geomAttrs = attrsFromElement(geomNode);
      const geomName = geomAttrs.name || geomAttrs.mesh || `${linkName}_geom_${geomIndex}`;
      const isVisual = classifyMujocoGeom(geomNode);
      let payloads = null;
      if (!isVisual) {
        payloads = shapePayloadForMujocoGeom(geomNode, matrixFromPoseAttrs(geomAttrs), geomName);
      }
      if (!payloads) payloads = await mujocoGeomPayloads(geomNode, meshAssets, geomName);

      const object = await loadMujocoGeomObject(geomNode, meshAssets, geomName);
      object.name = geomName;
      applyMujocoObjectDisplayTransform(object, geomNode, meshAssets);
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
        for (const payload of payloads) payload.approximation = 'none';
        linkPayload.collisions.push(...payloads);
        collisionCount += payloads.length;
      }
      geomIndex++;
    }

    links.push(linkPayload);

    for (const childBody of childElements(bodyNode, 'body')) {
      await visitBody(childBody, linkObject, linkName);
    }
  }

  for (const worldbody of childElements(root, 'worldbody')) {
    for (const bodyNode of childElements(worldbody, 'body')) {
      await visitBody(bodyNode, group);
    }
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
}

function currentFitObjects() {
  return [state.robot, state.usdObject].filter(Boolean);
}

function fitCamera(rootOrObjects) {
  const objects = Array.isArray(rootOrObjects) ? rootOrObjects : [rootOrObjects].filter(Boolean);
  const box = new THREE.Box3();
  for (const object of objects) box.expandByObject(object);
  if (!box.isEmpty()) {
    const size = box.getSize(new THREE.Vector3());
    const center = box.getCenter(new THREE.Vector3());
    const radius = Math.max(size.length() * 0.5, 0.5);
    controls.target.copy(center);
    camera.position.copy(center).add(new THREE.Vector3(radius * 1.1, radius * 0.75, radius * 1.35));
    camera.near = Math.max(radius / 1000, 0.001);
    camera.far = radius * 100;
    camera.updateProjectionMatrix();
      controls.update();
  }
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
  gui.add(state.settings, 'animateJoints').name('Animate joints');
  gui.add(state.settings, 'animationSpeed', 0.1, 4.0, 0.1).name('Animation speed');
  gui.add(state.settings, 'autocenter').name('Autocenter');
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
  if (state.robot?.setJointValue) {
    state.robot.setJointValue(jointName, value);
  } else if (joint.setJointValue) {
    joint.setJointValue(value);
  }
  syncGhosts();
}

function jointLimits(joint) {
  const type = joint.jointType || joint.type || 'fixed';
  const limit = joint.limit || {};
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
  if (!state.robot) return;
  state.joints = state.robot.joints || {};
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
        <div class="joint-name" title="${name}">${name}</div>
        <div class="joint-type">${type}</div>
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
    jointControlsEl.appendChild(row);
  }
}

function updateRobotInfo(metadata) {
  document.getElementById('robotName').textContent = metadata.name || state.robot?.name || '-';
  document.getElementById('linkCount').textContent = String(Object.keys(state.robot?.links || {}).length);
  document.getElementById('jointCount').textContent = String(Object.keys(state.robot?.joints || {}).length);
  document.getElementById('meshCount').textContent = String(state.visualMeshes.length + state.collisionMeshes.length);
}

function updateLabels() {
  const sourceName = state.inputName || 'URDF/MJCF';
  const sourceFormat = state.inputFormat ? state.inputFormat.toUpperCase() : 'URDF/MJCF';
  sourceLabelEl.textContent = `${sourceFormat}: ${sourceName}`;
  usdLabelEl.textContent = state.usdName ? `USD: ${state.usdName}` : 'USD';
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
      payload.approximation = 'none';
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
  if (geometry.type === 'box') {
    const scale = matrixScale(matrix);
    const base = Array.isArray(geometry.size) ? geometry.size : [2, 2, 2];
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
        dynamics: {
          damping: prim.properties?.['mjc:damping'],
          friction: prim.properties?.['mjc:frictionloss']
        }
      };
    });

  return {
    name: extracted.name || 'ConvertedFromUSDPhysics',
    upAxis: extracted.upAxis || 'Y',
    links,
    joints
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

function escapeXML(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function geometryXML(geometry = {}) {
  if (geometry.type === 'box') return `<box size="${vec(geometry.size, [1, 1, 1])}"/>`;
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

async function ensureNativeExporter() {
  if (!state.nativeModule) {
    setStatus('Loading TinyUSDZ WASM...');
    const mod = await import('./src/tinyusdz/tinyusdz.js');
    state.nativeModule = await mod.default();
  }
  if (!state.nativeExporter) {
    state.nativeExporter = new state.nativeModule.TinyUSDZLoaderNative();
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

async function createUSDBytesFromSource(format) {
  const native = await ensureNativeExporter();
  const payload = buildCurrentExportPayload();
  setStatus(`Creating USD Physics + MuJoCo stage for ${payload.name}...`);
  const ok = native.createURDFPhysicsScene(JSON.stringify(payload));
  if (!ok) throw new Error(native.error());

  if (format === 'usda') {
    const text = native.exportAsUSDA();
    if (!text) throw new Error(native.error());
    return {
      bytes: new TextEncoder().encode(text),
      blob: new Blob([text], { type: 'text/plain' }),
      filename: `${usdBaseName()}.usda`
    };
  }
  if (format === 'usdc') {
    const bytes = bytesFromNativeView(native.exportAsUSDC());
    if (!bytes) throw new Error(native.error());
    return {
      bytes,
      blob: new Blob([bytes], { type: 'application/octet-stream' }),
      filename: `${usdBaseName()}.usdc`
    };
  }
  const bytes = bytesFromNativeView(native.exportAsUSDZ());
  if (!bytes) throw new Error(native.error());
  return {
    bytes,
    blob: new Blob([bytes], { type: 'model/vnd.usdz+zip' }),
    filename: `${usdBaseName()}.usdz`
  };
}

async function createUSDBytesFromImportedUSD(format) {
  const native = await ensureNativeExporter();
  if (!state.latestUSDBytes) throw new Error('No imported or converted USD is loaded.');
  if (!native.loadFromBinary(state.latestUSDBytes, state.usdName || `scene.${state.latestUSDFormat || 'usd'}`)) {
    throw new Error(native.error() || 'Failed to load current USD for export.');
  }

  if (format === 'usda') {
    const text = native.exportAsUSDA();
    if (!text) throw new Error(native.error());
    return {
      bytes: new TextEncoder().encode(text),
      blob: new Blob([text], { type: 'text/plain' }),
      filename: `${usdBaseName()}.usda`
    };
  }
  if (format === 'usdc') {
    const bytes = bytesFromNativeView(native.exportAsUSDC());
    if (!bytes) throw new Error(native.error());
    return {
      bytes,
      blob: new Blob([bytes], { type: 'application/octet-stream' }),
      filename: `${usdBaseName()}.usdc`
    };
  }
  const bytes = bytesFromNativeView(native.exportAsUSDZ());
  if (!bytes) throw new Error(native.error());
  return {
    bytes,
    blob: new Blob([bytes], { type: 'model/vnd.usdz+zip' }),
    filename: `${usdBaseName()}.usdz`
  };
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
  const object = await loadUSDObjectFromBytes(result.bytes, result.filename);
  state.usdObject = object;
  state.usdName = result.filename;
  state.latestUSDBytes = result.bytes;
  state.latestUSDFormat = format;
  usdGroup.add(object);
  applySceneOrientation();
  updateButtonStates();
  rebuildGhosts();
  updateLabels();
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
  const urdf = usdPhysicsToUrdf(JSON.parse(jsonText));
  const urdfXML = urdfToXML(urdf);
  const file = new File([urdfXML], `${usdBaseName()}_from_usd.urdf`, { type: 'application/xml' });
  await loadRobotFile(file);
  setStatus(`Converted USD to URDF: ${urdf.links.length} links, ${urdf.joints.length} joints.`);
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
    setStatus(`Convert to URDF failed: ${err.message}`);
  });
});

document.getElementById('exportUSDA').addEventListener('click', () => exportRobot('usda'));
document.getElementById('exportUSDC').addEventListener('click', () => exportRobot('usdc'));
document.getElementById('exportUSDZ').addEventListener('click', () => exportRobot('usdz'));

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
  if (state.robot && state.settings.animateJoints) {
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
  syncGhosts();
  renderSplitView();
}

buildGUI();
updateButtonStates();
updateLabels();
animate();
