#!/usr/bin/env node
// URDF/MJCF -> USD Physics + MuJoCo + Newton export tester for TinyUSDZ WASM.

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import * as THREE from 'three';
import { OBJLoader } from 'three/examples/jsm/loaders/OBJLoader.js';
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js';
import { TinyUSDZLoader } from '../src/tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from '../src/tinyusdz/TinyUSDZLoaderUtils.js';
import TinyUSDZFactory from '../src/tinyusdz/tinyusdz.js';

const SUPPORTED_FORMATS = new Set(['usda', 'usdc', 'usdz', 'all']);
const USD_MESH_EXTENSIONS = new Set(['.usd', '.usda', '.usdc', '.usdz']);

const SAMPLE_URDF = `<?xml version="1.0"?>
<robot name="TinyUSDZSampleRobot">
  <link name="base_link">
    <inertial>
      <origin xyz="0 0 0"/>
      <mass value="1"/>
      <inertia ixx="1" iyy="1" izz="1"/>
    </inertial>
    <visual name="visual_mesh">
      <origin xyz="0 0 0" rpy="0 0 0"/>
      <geometry>
        <mesh filename="sample.obj" scale="1 1 1"/>
      </geometry>
    </visual>
    <collision name="collision_box">
      <origin xyz="0 0 0" rpy="0 0 0"/>
      <geometry>
        <box size="1 1 0.3"/>
      </geometry>
    </collision>
  </link>
  <link name="arm_link">
    <inertial>
      <origin xyz="0 0 0"/>
      <mass value="0.25"/>
      <inertia ixx="0.1" iyy="0.1" izz="0.1"/>
    </inertial>
    <visual name="arm_box">
      <origin xyz="0.4 0 0" rpy="0 0 0"/>
      <geometry>
        <box size="0.8 0.18 0.18"/>
      </geometry>
    </visual>
  </link>
  <joint name="hinge" type="revolute">
    <parent link="base_link"/>
    <child link="arm_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
    <axis xyz="0 0 1"/>
    <limit lower="-1.57" upper="1.57" effort="2" velocity="4"/>
    <dynamics damping="0.1" friction="0.02"/>
  </joint>
</robot>
`;

const SAMPLE_OBJ = `o sample_triangle
v 0 0 0
v 0.7 0 0
v 0 0.7 0
vn 0 0 1
vt 0 0
vt 1 0
vt 0 1
f 1/1/1 2/2/1 3/3/1
`;

function printHelp() {
  console.log(`
URDF/MJCF -> USD Physics + MuJoCo + Newton CLI tester

Usage:
  node cli/urdf-to-usd.js <robot.urdf> [options]
  node cli/urdf-to-usd.js <scene.xml> --input-format mjcf [options]
  node cli/urdf-to-usd.js --sample --format all -o /tmp/robot

Options:
  --format <fmt>       Export format: usda, usdc, usdz, all (default: usda)
  --input-format <fmt> Input format: auto, urdf, mjcf (default: auto)
  -o, --output <path>  Output file path, or base path when --format all
  --asset-dir <dir>    Add a directory used to resolve mesh assets
  --package-root <dir> Resolve package:// URIs under this directory
  --up-axis <axis>     Export up axis: Z or Y (default: Z)
  --allow-missing      Skip missing/unsupported meshes instead of failing
  --tessellate-collision-shapes
                       Tessellate primitive collision shapes to mesh.
                       Default: use USD native shape prims for primitive collisions.
  --max-usdc-mb <N>    Raise the USDC writer's max output size to N MB
                       (default WASM cap is 100MB). Use for mesh-dense scenes.
  --max-mem-mb <N>     Raise the USDC writer's max memory estimate to N MB.
  --dump-json <path>   Write the generated createURDFPhysicsScene JSON payload
  --no-verify          Do not verify expected USD/MuJoCo schema text
  --sample             Use an embedded URDF + OBJ smoke-test robot
  -v, --verbose        Print mesh and export details
  -h, --help           Show this help
`);
}

function parseArgs(argv = process.argv.slice(2)) {
  const opts = {
    inputFile: null,
    inputFormat: 'auto',
    format: 'usda',
    outputFile: null,
    assetDirs: [],
    packageRoot: null,
    upAxis: 'Z',
    allowMissing: false,
    tessellateCollisionShapes: false,
    dumpJson: null,
    maxUsdcMb: 0,
    maxMemMb: 0,
    verify: true,
    sample: false,
    verbose: false
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '-h' || arg === '--help') {
      printHelp();
      process.exit(0);
    } else if (arg === '--format') {
      opts.format = requireValue(argv, ++i, arg).toLowerCase();
      if (!SUPPORTED_FORMATS.has(opts.format)) {
        throw new Error('--format must be one of: usda, usdc, usdz, all');
      }
    } else if (arg === '--input-format') {
      opts.inputFormat = requireValue(argv, ++i, arg).toLowerCase();
      if (!['auto', 'urdf', 'mjcf'].includes(opts.inputFormat)) {
        throw new Error('--input-format must be one of: auto, urdf, mjcf');
      }
    } else if (arg === '-o' || arg === '--output') {
      opts.outputFile = requireValue(argv, ++i, arg);
    } else if (arg === '--asset-dir') {
      opts.assetDirs.push(requireValue(argv, ++i, arg));
    } else if (arg === '--package-root') {
      opts.packageRoot = requireValue(argv, ++i, arg);
    } else if (arg === '--up-axis') {
      opts.upAxis = requireValue(argv, ++i, arg).toUpperCase();
      if (opts.upAxis !== 'Z' && opts.upAxis !== 'Y') {
        throw new Error('--up-axis must be Z or Y');
      }
    } else if (arg === '--allow-missing') {
      opts.allowMissing = true;
    } else if (arg === '--tessellate-collision-shapes') {
      opts.tessellateCollisionShapes = true;
    } else if (arg === '--max-usdc-mb') {
      opts.maxUsdcMb = Number(requireValue(argv, ++i, arg)) || 0;
    } else if (arg === '--max-mem-mb') {
      opts.maxMemMb = Number(requireValue(argv, ++i, arg)) || 0;
    } else if (arg === '--dump-json') {
      opts.dumpJson = requireValue(argv, ++i, arg);
    } else if (arg === '--no-verify') {
      opts.verify = false;
    } else if (arg === '--sample') {
      opts.sample = true;
    } else if (arg === '-v' || arg === '--verbose') {
      opts.verbose = true;
    } else if (arg.startsWith('-')) {
      throw new Error(`Unknown option: ${arg}`);
    } else if (!opts.inputFile) {
      opts.inputFile = arg;
    } else {
      throw new Error(`Unexpected argument: ${arg}`);
    }
  }

  if (!opts.inputFile && !opts.sample) {
    throw new Error('Input URDF is required, or use --sample.');
  }

  return opts;
}

function requireValue(argv, index, optionName) {
  const value = argv[index];
  if (!value || value.startsWith('-')) {
    throw new Error(`${optionName} requires a value`);
  }
  return value;
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

function decodeXML(text) {
  return text
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

function findElements(xml, tagName) {
  const elements = [];
  const re = new RegExp(`<${tagName}\\b([^>]*?)(?:/\\s*>|>([\\s\\S]*?)</${tagName}\\s*>)`, 'gi');
  let match = null;
  while ((match = re.exec(xml))) {
    elements.push({
      attrs: parseAttributes(match[1] || ''),
      body: match[2] || ''
    });
  }
  return elements;
}

function firstElement(xml, tagName) {
  return findElements(xml, tagName)[0] || null;
}

function parseXMLTree(xml) {
  const root = { name: '#document', attrs: {}, children: [], parent: null };
  const stack = [root];
  const tokenRe = /<!--[\s\S]*?-->|<\?[\s\S]*?\?>|<!\[CDATA\[[\s\S]*?\]\]>|<![^>]*>|<\/\s*([A-Za-z_:][-A-Za-z0-9_:.]*)\s*>|<\s*([A-Za-z_:][-A-Za-z0-9_:.]*)([^>]*?)(\/?)>/g;
  let match = null;
  while ((match = tokenRe.exec(xml))) {
    if (!match[0].startsWith('<') || match[0].startsWith('<!--') || match[0].startsWith('<?') || match[0].startsWith('<!')) {
      continue;
    }
    if (match[1]) {
      const closing = match[1];
      while (stack.length > 1 && stack[stack.length - 1].name !== closing) {
        stack.pop();
      }
      if (stack.length > 1) stack.pop();
      continue;
    }
    const node = {
      name: match[2],
      attrs: parseAttributes(match[3] || ''),
      children: [],
      parent: stack[stack.length - 1]
    };
    stack[stack.length - 1].children.push(node);
    if (!match[4]) stack.push(node);
  }
  return root.children[0] || root;
}

function childElements(node, name = null) {
  return (node?.children || []).filter((child) => !name || child.name === name);
}

function firstChild(node, name) {
  return childElements(node, name)[0] || null;
}

function numberAttr(attrs, name, fallback = undefined) {
  const value = Number(attrs?.[name]);
  return Number.isFinite(value) ? value : fallback;
}

function parseNumbers(text, fallback = []) {
  if (!text) return fallback;
  const values = text.trim().split(/\s+/).map(Number).filter(Number.isFinite);
  return values.length ? values : fallback;
}

function elementText(el) {
  return (el?.body || '').replace(/<[^>]*>/g, ' ').trim();
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

// MuJoCo <compiler> context for angle units + euler sequence. Set per-parse in
// buildMujocoPayload; defaults match MuJoCo (degrees, "xyz").
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

function originToMatrix(originAttrs = {}) {
  const xyz = parseNumbers(originAttrs.xyz, [0, 0, 0]);
  const rpy = parseNumbers(originAttrs.rpy, [0, 0, 0]);
  return new THREE.Matrix4().compose(
    new THREE.Vector3(xyz[0] || 0, xyz[1] || 0, xyz[2] || 0),
    new THREE.Quaternion().setFromEuler(new THREE.Euler(rpy[0] || 0, rpy[1] || 0, rpy[2] || 0, 'XYZ')),
    new THREE.Vector3(1, 1, 1)
  );
}

function axisToToken(axis) {
  const abs = axis.map((v) => Math.abs(v));
  const max = Math.max(abs[0] || 0, abs[1] || 0, abs[2] || 0);
  if (max === abs[1]) return 'Y';
  if (max === abs[2]) return 'Z';
  return 'X';
}

function parseURDFMetadata(urdfText) {
  const robotEl = firstElement(urdfText, 'robot');
  const links = new Map();
  const joints = [];
  const actuators = [];

  for (const linkEl of findElements(urdfText, 'link')) {
    const name = linkEl.attrs.name || `link_${links.size}`;
    const inertialEl = firstElement(linkEl.body, 'inertial');
    const inertial = {};
    if (inertialEl) {
      const massEl = firstElement(inertialEl.body, 'mass');
      const originEl = firstElement(inertialEl.body, 'origin');
      const inertiaEl = firstElement(inertialEl.body, 'inertia');
      if (massEl) inertial.mass = numberAttr(massEl.attrs, 'value', 0);
      if (originEl) inertial.centerOfMass = parseNumbers(originEl.attrs.xyz, [0, 0, 0]);
      if (inertiaEl) {
        inertial.diagonalInertia = [
          numberAttr(inertiaEl.attrs, 'ixx', 0),
          numberAttr(inertiaEl.attrs, 'iyy', 0),
          numberAttr(inertiaEl.attrs, 'izz', 0)
        ];
      }
    }
    links.set(name, { name, inertial, body: linkEl.body });
  }

  for (const jointEl of findElements(urdfText, 'joint')) {
    const parent = firstElement(jointEl.body, 'parent')?.attrs.link || '';
    const child = firstElement(jointEl.body, 'child')?.attrs.link || '';
    const axis = parseNumbers(firstElement(jointEl.body, 'axis')?.attrs.xyz, [1, 0, 0]);
    const originEl = firstElement(jointEl.body, 'origin');
    const origin = parseNumbers(originEl?.attrs.xyz, [0, 0, 0]);
    const limitEl = firstElement(jointEl.body, 'limit');
    const dynamicsEl = firstElement(jointEl.body, 'dynamics');
    const joint = {
      name: jointEl.attrs.name || `joint_${joints.length}`,
      type: jointEl.attrs.type || 'fixed',
      parent,
      child,
      axis,
      axisToken: axisToToken(axis),
      origin,
      originMatrix: matrixToUSDArray(originToMatrix(originEl?.attrs)),
      limit: limitEl ? {
        lower: numberAttr(limitEl.attrs, 'lower'),
        upper: numberAttr(limitEl.attrs, 'upper'),
        effort: numberAttr(limitEl.attrs, 'effort'),
        velocity: numberAttr(limitEl.attrs, 'velocity')
      } : {},
      // URDF 1.0 <dynamics> spec only defines damping + friction, but
      // we accept the common MuJoCo extensions stiffness and armature
      // when authored (no-op for compliant URDFs). The downstream
      // C++ converter (urdf-to-usd.cc) emits these via the PhysX
      // mirror schema (physxJoint:armature, physxLimit:angular:stiffness)
      // alongside the canonical mjc:* fallbacks.
      dynamics: dynamicsEl ? {
        damping: numberAttr(dynamicsEl.attrs, 'damping'),
        friction: numberAttr(dynamicsEl.attrs, 'friction'),
        stiffness: numberAttr(dynamicsEl.attrs, 'stiffness'),
        armature: numberAttr(dynamicsEl.attrs, 'armature')
      } : {}
    };
    const mimicEl = firstElement(jointEl.body, 'mimic');
    if (mimicEl?.attrs?.joint) {
      joint.mimic = {
        joint: mimicEl.attrs.joint,
        multiplier: numberAttr(mimicEl.attrs, 'multiplier', 1),
        offset: numberAttr(mimicEl.attrs, 'offset', 0)
      };
      // Parsed for completeness, but USD physics has no direct mimic analog and
      // the converter does not export the coupling — make that visible.
      console.warn(`URDF joint "${joint.name}" has a <mimic> (couples to `
        + `"${mimicEl.attrs.joint}"); mimic coupling is not exported to USD.`);
    }
    joints.push(joint);
  }

  const jointsByName = new Map(joints.map((joint) => [joint.name, joint]));
  for (const transmissionEl of findElements(urdfText, 'transmission')) {
    const jointName = firstElement(transmissionEl.body, 'joint')?.attrs.name || '';
    if (!jointName) continue;
    const actuatorEl = firstElement(transmissionEl.body, 'actuator');
    const reduction = Number(elementText(firstElement(actuatorEl?.body || '', 'mechanicalReduction')));
    const joint = jointsByName.get(jointName);
    const act = {
      name: actuatorEl?.attrs.name || `${jointName}_actuator`,
      joint: jointName,
      control: 'pd'
    };
    const effort = joint?.limit?.effort;
    if (Number.isFinite(effort) && effort > 0) act.maxEffort = effort;
    if (Number.isFinite(reduction) && reduction !== 0) act.constEffort = Math.abs(reduction);
    actuators.push(act);
  }

  return {
    name: robotEl?.attrs.name || '',
    links,
    joints,
    actuators
  };
}

class MeshResolver {
  constructor(opts, urdfDir) {
    this.opts = opts;
    this.urdfDir = urdfDir;
    this.virtualFiles = new Map();
    if (opts.sample) {
      this.virtualFiles.set('sample.obj', SAMPLE_OBJ);
    }
  }

  resolve(meshPath) {
    const normalized = normalizePath(meshPath || '');
    if (!normalized) return null;
    if (this.virtualFiles.has(normalized)) {
      return { path: normalized, virtualText: this.virtualFiles.get(normalized) };
    }

    const withoutPackage = normalized.replace(/^package:\/\//, '');
    const candidates = [];
    if (path.isAbsolute(normalized)) candidates.push(normalized);
    candidates.push(path.resolve(this.urdfDir, normalized));
    candidates.push(path.resolve(this.urdfDir, withoutPackage));
    candidates.push(path.resolve(this.urdfDir, withoutPackage.split('/').slice(1).join('/')));
    for (const assetDir of this.opts.assetDirs) {
      candidates.push(path.resolve(assetDir, normalized));
      candidates.push(path.resolve(assetDir, withoutPackage));
      candidates.push(path.resolve(assetDir, withoutPackage.split('/').slice(1).join('/')));
      candidates.push(path.resolve(assetDir, path.basename(withoutPackage)));
    }
    if (this.opts.packageRoot) {
      candidates.push(path.resolve(this.opts.packageRoot, withoutPackage));
      candidates.push(path.resolve(this.opts.packageRoot, withoutPackage.split('/').slice(1).join('/')));
    }

    for (const candidate of candidates.filter(Boolean)) {
      if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
        return { path: candidate };
      }
    }
    return null;
  }
}

function normalizePath(value) {
  return value.replace(/\\/g, '/').replace(/^\.?\//, '');
}

function extension(filename) {
  return path.extname((filename || '').split('?')[0].split('#')[0]).toLowerCase();
}

function asFloat32Array(values) {
  return values instanceof Float32Array ? values : new Float32Array(values || []);
}

function asInt32Array(values) {
  return values instanceof Int32Array ? values : new Int32Array(values || []);
}

// Mesh geometry is marshalled to the WASM binding as binary typed arrays via
// setVisualMesh/setCollisionMesh (referenced by `meshRef` in the JSON payload),
// not inlined as JSON number arrays. This keeps the payload string tiny and
// avoids V8's max-string-length ceiling on mesh-dense models. Populated by
// makeGeometryPayload, registered in main() before createURDFPhysicsScene.
const meshBuffers = new Map(); // meshRef -> { positions, normals, uvs, indices }
let meshRefCounter = 0;

function makeGeometryPayload(mesh, matrix, name) {
  const geom = mesh.geometry;
  const pos = geom?.getAttribute('position');
  if (!pos || pos.count < 3) return null;
  const normal = geom.getAttribute('normal');
  const uv = geom.getAttribute('uv');
  const index = geom.getIndex();
  const meshRef = `mesh_${meshRefCounter++}`;
  meshBuffers.set(meshRef, {
    positions: asFloat32Array(pos.array),
    normals: normal ? asFloat32Array(normal.array) : new Float32Array(),
    uvs: uv ? asFloat32Array(uv.array) : new Float32Array(),
    indices: index ? asInt32Array(index.array) : new Int32Array()
  });
  return {
    name,
    matrix: matrixToUSDArray(matrix),
    meshRef
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
  const mesh = new THREE.Mesh(geometry);
  mesh.name = name;
  const group = new THREE.Group();
  group.add(mesh);
  return group;
}

// Merge every sub-mesh of `object` (e.g. an OBJ split into many `o`/`g`
// objects, as in ms_human_700's Rib1L.obj) into a single mesh. A MuJoCo
// <mesh> is one mesh regardless of how the source file is grouped, matching
// the native loader which merges all `v`/`f` records.
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
  object.traverse((obj) => {
    if (!obj.isMesh || !obj.geometry) return;
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
  const geom = new THREE.BufferGeometry();
  geom.setAttribute('position', new THREE.Float32BufferAttribute(positions, 3));
  if (hasNormals && normals.length) geom.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  if (hasUVs && uvs.length) geom.setAttribute('uv', new THREE.Float32BufferAttribute(uvs, 2));
  geom.setIndex(indices);
  return primitiveObjectFromGeometry(geom, name);
}

async function loadOBJObject(resolvedAsset) {
  const text = resolvedAsset.virtualText ?? fs.readFileSync(resolvedAsset.path, 'utf8');
  return new OBJLoader().parse(text);
}

async function loadSTLObject(resolvedAsset) {
  const data = fs.readFileSync(resolvedAsset.path);
  const arrayBuffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
  return primitiveObjectFromGeometry(new STLLoader().parse(arrayBuffer), path.basename(resolvedAsset.path, path.extname(resolvedAsset.path)));
}

let usdMeshLoader = null;

async function ensureUSDMeshLoader() {
  if (!usdMeshLoader) {
    usdMeshLoader = new TinyUSDZLoader();
    await usdMeshLoader.init({ useZstdCompressedWasm: false, useMemory64: false });
    TinyUSDZLoaderUtils.setTinyUSDZ(usdMeshLoader.native_);
  }
  return usdMeshLoader;
}

async function loadUSDObject(resolvedAsset) {
  const loader = await ensureUSDMeshLoader();
  const data = fs.readFileSync(resolvedAsset.path);
  const sceneData = await parseUSDSceneFromArrayBuffer(
    loader,
    data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength),
    path.basename(resolvedAsset.path)
  );
  const rootNode = sceneData.getDefaultRootNode();
  const defaultMaterial = TinyUSDZLoaderUtils.createDefaultMaterial();
  return TinyUSDZLoaderUtils.buildThreeNode(rootNode, defaultMaterial, sceneData, {
    overrideMaterial: false
  });
}

async function parseUSDSceneFromArrayBuffer(loader, arrayBuffer, filename) {
  return new Promise((resolve, reject) => {
    loader.parse(new Uint8Array(arrayBuffer), filename, resolve, reject);
  });
}

async function meshPayloadsForGeometry(geometryEl, originMatrix, resolver, fallbackName, opts) {
  const meshEl = firstElement(geometryEl.body, 'mesh');
  if (meshEl) {
    const filename = meshEl.attrs.filename || meshEl.attrs.url || '';
    const resolved = resolver.resolve(filename);
    if (!resolved) {
      if (opts.allowMissing) {
        console.warn(`Skipping missing mesh: ${filename}`);
        return [];
      }
      throw new Error(`Mesh asset not found: ${filename}`);
    }

    const scale = parseNumbers(meshEl.attrs.scale, [1, 1, 1]);
    const meshMatrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
    const ext = extension(resolved.path || filename);
    let object = null;
    if (ext === '.obj') {
      object = await loadOBJObject(resolved);
    } else if (ext === '.stl') {
      object = await loadSTLObject(resolved);
    } else if (USD_MESH_EXTENSIONS.has(ext)) {
      object = await loadUSDObject(resolved);
    } else if (opts.allowMissing) {
      console.warn(`Skipping unsupported mesh extension ${ext || '(none)'}: ${filename}`);
      return [];
    } else {
      throw new Error(`Unsupported mesh extension ${ext || '(none)'}: ${filename}`);
    }
    return collectMeshPayloads(object, meshMatrix, fallbackName);
  }

  const boxEl = firstElement(geometryEl.body, 'box');
  if (boxEl) {
    const size = parseNumbers(boxEl.attrs.size, [1, 1, 1]);
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.BoxGeometry(size[0] || 1, size[1] || 1, size[2] || 1), fallbackName),
      originMatrix,
      fallbackName
    );
  }

  const sphereEl = firstElement(geometryEl.body, 'sphere');
  if (sphereEl) {
    const radius = numberAttr(sphereEl.attrs, 'radius', 0.5);
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.SphereGeometry(radius, 24, 12), fallbackName),
      originMatrix,
      fallbackName
    );
  }

  const cylinderEl = firstElement(geometryEl.body, 'cylinder');
  if (cylinderEl) {
    const radius = numberAttr(cylinderEl.attrs, 'radius', 0.5);
    const length = numberAttr(cylinderEl.attrs, 'length', 1);
    const yToZ = new THREE.Matrix4().makeRotationX(Math.PI / 2);
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.CylinderGeometry(radius, radius, length, 24, 1), fallbackName),
      new THREE.Matrix4().copy(originMatrix).multiply(yToZ),
      fallbackName
    );
  }

  if (opts.allowMissing) {
    console.warn(`Skipping unsupported geometry block: ${fallbackName}`);
    return [];
  }
  throw new Error(`Unsupported geometry block: ${fallbackName}`);
}

function shapePayloadForUrdfGeometry(geometryEl, originMatrix, fallbackName) {
  const boxEl = firstElement(geometryEl.body, 'box');
  if (boxEl) {
    const size = parseNumbers(boxEl.attrs.size, [1, 1, 1]);
    const half = [(size[0] || 1) * 0.5, (size[1] || 1) * 0.5, (size[2] || 1) * 0.5];
    const matrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(half[0], half[1], half[2]));
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(matrix),
      shape: { type: 'box' }
    }];
  }

  const sphereEl = firstElement(geometryEl.body, 'sphere');
  if (sphereEl) {
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: {
        type: 'sphere',
        radius: numberAttr(sphereEl.attrs, 'radius', 0.5)
      }
    }];
  }

  const cylinderEl = firstElement(geometryEl.body, 'cylinder');
  if (cylinderEl) {
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: {
        type: 'cylinder',
        radius: numberAttr(cylinderEl.attrs, 'radius', 0.5),
        height: numberAttr(cylinderEl.attrs, 'length', 1),
        axis: 'Z'
      }
    }];
  }

  return null;
}

function expandMujocoIncludes(xml, baseDir, seen = new Set()) {
  return xml.replace(/<include\b([^>]*?)\/\s*>/gi, (_tag, attrText) => {
    const attrs = parseAttributes(attrText || '');
    if (!attrs.file) return '';
    const includePath = path.resolve(baseDir, attrs.file);
    if (seen.has(includePath)) {
      throw new Error(`Recursive MJCF include: ${includePath}`);
    }
    seen.add(includePath);
    const childXML = stripMujocoDocumentRoot(fs.readFileSync(includePath, 'utf8'));
    seen.delete(includePath);
    return expandMujocoIncludes(childXML, path.dirname(includePath), seen);
  });
}

function stripMujocoDocumentRoot(xml) {
  const withoutDecl = xml.replace(/<\?xml[\s\S]*?\?>/i, '');
  const open = withoutDecl.search(/<mujoco\b[^>]*>/i);
  const close = withoutDecl.lastIndexOf('</mujoco>');
  if (open < 0 || close < 0) return withoutDecl;
  const openEnd = withoutDecl.indexOf('>', open);
  return withoutDecl.slice(openEnd + 1, close);
}

// Basename -> path index of mesh files under baseDir. Last-resort fallback for
// scenes whose <mesh file> paths are relative to an included file's directory
// (e.g. ms_human_700: assets/asset/*.xml referencing "../geometry/*.stl").
// Names appearing in more than one place are dropped to avoid ambiguity.
function buildMeshIndex(baseDir) {
  const index = new Map();
  const ambiguous = new Set();
  const walk = (dir) => {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (entry.isFile()) {
        const ext = path.extname(entry.name).toLowerCase();
        if (ext !== '.stl' && ext !== '.obj' && ext !== '.msh') continue;
        const key = entry.name.toLowerCase();
        if (index.has(key)) ambiguous.add(key);
        else index.set(key, full);
      }
    }
  };
  walk(baseDir);
  for (const key of ambiguous) index.delete(key);
  return index;
}

// Resolve a MJCF mesh file, tolerating models that omit <compiler meshdir>
// but keep assets under an "assets/" subdir (e.g. skydio_x2, google_robot).
// Falls back to --asset-dir entries and a recursive basename index; returns the
// primary candidate (which may not exist) when nothing resolves so the caller
// can honor --allow-missing.
function resolveMujocoMeshFile(file, meshBaseDir, baseDir, opts, meshIndex) {
  const candidates = [
    path.resolve(meshBaseDir, file),
    path.resolve(baseDir, file),
    path.resolve(baseDir, 'assets', file),
    path.resolve(baseDir, path.basename(file))
  ];
  for (const dir of (opts?.assetDirs || [])) {
    candidates.push(path.resolve(dir, file));
    candidates.push(path.resolve(dir, path.basename(file)));
  }
  for (const candidate of candidates) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
  }
  const indexed = meshIndex?.get(path.basename(file).toLowerCase());
  if (indexed) return indexed;
  return candidates[0];
}

function collectMujocoAssets(root, baseDir, opts) {
  const compiler = firstChild(root, 'compiler');
  // <compiler meshdir> wins; assetdir is the shared meshdir/texturedir default.
  const meshDir = compiler?.attrs.meshdir || compiler?.attrs.assetdir || '';
  const meshBaseDir = path.resolve(baseDir, meshDir);
  const meshIndex = buildMeshIndex(baseDir);
  const meshes = new Map();
  for (const asset of childElements(root, 'asset')) {
    for (const mesh of childElements(asset, 'mesh')) {
      const file = mesh.attrs.file || '';
      if (!file) continue;
      const name = mesh.attrs.name || path.basename(file, path.extname(file));
      meshes.set(name, {
        path: resolveMujocoMeshFile(file, meshBaseDir, baseDir, opts, meshIndex),
        scale: parseNumbers(mesh.attrs.scale, [1, 1, 1]),
        refpos: parseNumbers(mesh.attrs.refpos, [0, 0, 0]),
        refquat: parseNumbers(mesh.attrs.refquat, [1, 0, 0, 0])
      });
    }
  }
  return meshes;
}

function mujocoJointType(type) {
  if (type === 'hinge') return 'revolute';
  if (type === 'slide') return 'prismatic';
  if (type === 'free') return 'floating';
  // ball (3-DOF rotation) -> USD PhysicsSphericalJoint (matches web/js/urdf.js).
  if (type === 'ball') return 'spherical';
  return 'fixed';
}

function parseMujocoInertial(bodyNode) {
  const inertialNode = firstChild(bodyNode, 'inertial');
  if (!inertialNode) return {};
  const full = parseNumbers(inertialNode.attrs.fullinertia, []);
  const inertial = {
    mass: numberAttr(inertialNode.attrs, 'mass', 0),
    centerOfMass: parseNumbers(inertialNode.attrs.pos, [0, 0, 0])
  };
  if (full.length >= 3) {
    inertial.diagonalInertia = [full[0], full[1], full[2]];
    if (full.length >= 6 && full.slice(3, 6).some((v) => Math.abs(v) > 1e-9)) {
      console.warn(`MJCF body "${bodyNode.attrs?.name || '?'}" has a non-diagonal `
        + 'fullinertia; off-diagonal terms are dropped (only the diagonal is exported).');
    }
  } else {
    // `diaginertia` is "Ixx Iyy Izz" — parse all three (numberAttr only reads a
    // single Number(), which yields NaN for the multi-value string and silently
    // dropped the inertia). Mirrors web/js/urdf.js::parseMujocoInertial.
    inertial.diagonalInertia = parseNumbers(inertialNode.attrs.diaginertia, []);
  }
  return inertial;
}

function shapePayloadForMujocoGeom(geomNode, originMatrix, fallbackName, bodyWorld = new THREE.Matrix4()) {
  const geomType = geomNode.attrs.type || (geomNode.attrs.mesh ? 'mesh' : 'sphere');
  if (geomType === 'mesh') return null;

  const size = parseNumbers(geomNode.attrs.size, []);
  if (geomType === 'box') {
    const half = [size[0] || 1, size[1] || 1, size[2] || 1];
    const matrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(half[0], half[1], half[2]));
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(matrix),
      shape: { type: 'box' }
    }];
  }

  if (geomType === 'sphere') {
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(originMatrix),
      shape: {
        type: 'sphere',
        radius: size[0] || 0.5
      }
    }];
  }

  if (geomType === 'ellipsoid') {
    const radii = [size[0] || 0.5, size[1] || 0.5, size[2] || 0.5];
    const matrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(radii[0], radii[1], radii[2]));
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(matrix),
      shape: {
        type: 'sphere',
        radius: 1
      }
    }];
  }

  if (geomType === 'cylinder' || geomType === 'capsule') {
    const fromto = parseNumbers(geomNode.attrs.fromto, []);
    let matrix = new THREE.Matrix4().copy(originMatrix);
    let height = size[1] ? size[1] * 2 : 1;
    if (fromto.length >= 6) {
      // fromto spans p1->p2 (pos/quat ignored): center at the midpoint and
      // orient the cylinder's Z axis along the segment.
      const p1 = new THREE.Vector3(fromto[0], fromto[1], fromto[2]);
      const p2 = new THREE.Vector3(fromto[3], fromto[4], fromto[5]);
      const dir = p2.clone().sub(p1);
      height = dir.length() || 1;
      const center = p1.clone().add(p2).multiplyScalar(0.5);
      const quatZ = new THREE.Quaternion().setFromUnitVectors(
        new THREE.Vector3(0, 0, 1), dir.clone().normalize());
      // fromto replaces the geom's pose but is still placed in body world space.
      matrix = new THREE.Matrix4().copy(bodyWorld).multiply(
        new THREE.Matrix4().compose(center, quatZ, new THREE.Vector3(1, 1, 1)));
    }
    return [{
      name: fallbackName,
      matrix: matrixToUSDArray(matrix),
      shape: {
        type: geomType,
        radius: size[0] || 0.5,
        height,
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

function addMujocoPhysicsAttrs(payload, geomNode) {
  if (!payload || !geomNode?.attrs) return payload;
  const group = numberAttr(geomNode.attrs, 'group');
  if (Number.isFinite(group)) payload.group = group;
  const condim = numberAttr(geomNode.attrs, 'condim');
  if (Number.isFinite(condim)) payload.condim = condim;
  const margin = numberAttr(geomNode.attrs, 'margin');
  const solmix = numberAttr(geomNode.attrs, 'solmix');
  if (Number.isFinite(margin) || Number.isFinite(solmix)) {
    payload.mjc = payload.mjc || {};
    if (Number.isFinite(margin)) payload.mjc.margin = margin;
    if (Number.isFinite(solmix)) payload.mjc.solmix = solmix;
  }
  return payload;
}

function buildMujocoActuators(root) {
  const actuators = [];
  for (const actuatorRoot of childElements(root, 'actuator')) {
    for (const actNode of childElements(actuatorRoot)) {
      const joint = actNode.attrs.joint || '';
      if (!joint) continue;
      const act = {
        name: actNode.attrs.name || `${actNode.name}_${joint}`,
        joint,
        control: 'pd'
      };
      const kp = numberAttr(actNode.attrs, 'kp');
      if (Number.isFinite(kp)) {
        act.kp = kp;
      } else {
        const gain = parseNumbers(actNode.attrs.gainprm, []);
        if (gain.length) act.kp = gain[0];
      }
      const kd = numberAttr(actNode.attrs, 'kv');
      if (Number.isFinite(kd)) {
        act.kd = kd;
      } else {
        const bias = parseNumbers(actNode.attrs.biasprm, []);
        if (bias.length >= 3) act.kd = Math.abs(bias[2]);
      }
      const forceRange = parseNumbers(actNode.attrs.forcerange, []);
      if (forceRange.length >= 2) {
        act.maxEffort = Math.max(Math.abs(forceRange[0]), Math.abs(forceRange[1]));
      }
      const ctrlRange = parseNumbers(actNode.attrs.ctrlrange, []);
      if (actNode.name === 'motor' && ctrlRange.length >= 2) {
        act.constEffort = Math.max(Math.abs(ctrlRange[0]), Math.abs(ctrlRange[1]));
      }
      const delay = numberAttr(actNode.attrs, 'delay');
      if (Number.isFinite(delay)) act.delaySteps = Math.max(1, Math.round(delay));
      actuators.push(act);
    }
  }
  return actuators;
}

async function mujocoGeomPayloads(geomNode, meshAssets, fallbackName, opts, bodyWorld = new THREE.Matrix4()) {
  const geomType = geomNode.attrs.type || (geomNode.attrs.mesh ? 'mesh' : 'sphere');
  // Bake the body-chain world transform into the geom matrix: the USD converter
  // places every link Xform at identity, so each geom carries its full world
  // placement (body_world * geom-local pose).
  const originMatrix = new THREE.Matrix4().copy(bodyWorld).multiply(matrixFromPoseAttrs(geomNode.attrs));
  if (geomType === 'mesh') {
    const meshName = geomNode.attrs.mesh;
    const meshAsset = meshAssets.get(meshName);
    if (!meshAsset) {
      if (opts.allowMissing) {
        console.warn(`Skipping missing MJCF mesh asset: ${meshName}`);
        return [];
      }
      throw new Error(`MJCF mesh asset not found: ${meshName}`);
    }
    if (!fs.existsSync(meshAsset.path)) {
      if (opts.allowMissing) {
        console.warn(`Skipping MJCF mesh file not found on disk: ${meshAsset.path}`);
        return [];
      }
      throw new Error(`MJCF mesh file not found: ${meshAsset.path}`);
    }
    const ext = extension(meshAsset.path);
    let object = null;
    if (ext === '.stl') {
      object = await loadSTLObject(meshAsset);
    } else if (ext === '.obj') {
      object = await loadOBJObject(meshAsset);
    } else if (USD_MESH_EXTENSIONS.has(ext)) {
      object = await loadUSDObject(meshAsset);
    } else if (opts.allowMissing) {
      console.warn(`Skipping unsupported MJCF mesh extension ${ext || '(none)'}: ${meshAsset.path}`);
      return [];
    } else {
      throw new Error(`Unsupported MJCF mesh extension ${ext || '(none)'}: ${meshAsset.path}`);
    }
    const scale = parseNumbers(geomNode.attrs.scale, meshAsset.scale || [1, 1, 1]);
    // MuJoCo mesh refpos/refquat: translate vertices by -refpos and rotate by
    // the conjugate of refquat before placement (e.g. shadow_dexee fingers).
    const rq = meshAsset.refquat || [1, 0, 0, 0];
    const rp = meshAsset.refpos || [0, 0, 0];
    const refQuat = new THREE.Quaternion(rq[1] || 0, rq[2] || 0, rq[3] || 0, rq[0] ?? 1).normalize().conjugate();
    const meshMatrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeRotationFromQuaternion(refQuat))
      .multiply(new THREE.Matrix4().makeTranslation(-(rp[0] || 0), -(rp[1] || 0), -(rp[2] || 0)))
      .multiply(new THREE.Matrix4().makeScale(scale[0] || 1, scale[1] || 1, scale[2] || 1));
    // A MuJoCo <mesh> is a single mesh: merge any multi-object source file
    // (e.g. ms_human_700 Rib1L.obj's 12 `o` groups) into one, matching the
    // native loader and MuJoCo semantics.
    return collectMeshPayloads(flattenToSingleMesh(object, fallbackName), meshMatrix, fallbackName);
  }

  const size = parseNumbers(geomNode.attrs.size, []);
  if (geomType === 'box') {
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.BoxGeometry((size[0] || 1) * 2, (size[1] || 1) * 2, (size[2] || 1) * 2), fallbackName),
      originMatrix,
      fallbackName
    );
  }
  if (geomType === 'sphere') {
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.SphereGeometry(size[0] || 0.5, 24, 12), fallbackName),
      originMatrix,
      fallbackName
    );
  }
  if (geomType === 'ellipsoid') {
    const radii = [size[0] || 0.5, size[1] || 0.5, size[2] || 0.5];
    const matrix = new THREE.Matrix4()
      .copy(originMatrix)
      .multiply(new THREE.Matrix4().makeScale(radii[0], radii[1], radii[2]));
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.SphereGeometry(1, 24, 12), fallbackName),
      matrix,
      fallbackName
    );
  }
  if (geomType === 'cylinder' || geomType === 'capsule') {
    const radius = size[0] || 0.5;
    const length = size[1] ? size[1] * 2 : 1;
    const yToZ = new THREE.Matrix4().makeRotationX(Math.PI / 2);
    return collectMeshPayloads(
      primitiveObjectFromGeometry(new THREE.CylinderGeometry(radius, radius, length, 24, 1), fallbackName),
      new THREE.Matrix4().copy(originMatrix).multiply(yToZ),
      fallbackName
    );
  }

  if (opts.allowMissing) {
    console.warn(`Skipping unsupported MJCF geom type: ${geomType}`);
    return [];
  }
  throw new Error(`Unsupported MJCF geom type: ${geomType}`);
}

// Resolve MuJoCo <default> class inheritance into per-class attribute tables for
// geoms and joints. Nested <default class="..."> inherit their parent default's
// merged attrs; the unnamed top-level <default> is the root baseline. This lets
// us recover attributes (group/contype/type/...) that real menagerie models set
// via classes/childclass rather than per-element, e.g. visual geoms tagged only
// through `childclass="robot"` + `<default class="robot"><geom group="2"/>`.
function parseMujocoDefaults(root) {
  const geom = new Map();
  const joint = new Map();
  let rootGeom = {};
  let rootJoint = {};
  function walk(defNode, inheritedGeom, inheritedJoint) {
    const gEl = firstChild(defNode, 'geom');
    const myGeom = { ...inheritedGeom, ...(gEl ? gEl.attrs : {}) };
    const jEl = firstChild(defNode, 'joint');
    const myJoint = { ...inheritedJoint, ...(jEl ? jEl.attrs : {}) };
    const cls = defNode.attrs.class;
    if (cls) {
      geom.set(cls, myGeom);
      joint.set(cls, myJoint);
    } else {
      rootGeom = myGeom;
      rootJoint = myJoint;
    }
    for (const child of childElements(defNode, 'default')) {
      walk(child, myGeom, myJoint);
    }
  }
  for (const defNode of childElements(root, 'default')) {
    walk(defNode, {}, {});
  }
  return { geom, joint, rootGeom, rootJoint };
}

// Effective attrs = class/childclass defaults merged under the element's own attrs.
function resolveElementAttrs(node, classTable, rootAttrs, childclass) {
  const cls = node.attrs.class || childclass;
  const base = cls && classTable.has(cls) ? classTable.get(cls) : rootAttrs;
  return { ...base, ...node.attrs };
}

async function buildMujocoPayload(xmlText, opts, baseDir) {
  const expanded = expandMujocoIncludes(xmlText, baseDir);
  const root = parseXMLTree(expanded);
  if (root.name !== 'mujoco') {
    throw new Error('Expected <mujoco> root for MJCF input.');
  }

  // Honor <compiler angle="..." eulerseq="..."> for all orientation specifiers.
  const compilerEl = firstChild(root, 'compiler');
  const angleAttr = (compilerEl?.attrs?.angle || 'degree').toLowerCase();
  mjcfPoseCtx = {
    toRad: angleAttr === 'radian' ? 1 : Math.PI / 180,
    eulerseq: compilerEl?.attrs?.eulerseq || 'xyz'
  };

  const defaults = parseMujocoDefaults(root);
  const meshAssets = collectMujocoAssets(root, baseDir, opts);
  const worldbody = firstChild(root, 'worldbody');
  if (!worldbody) {
    throw new Error('MJCF input has no <worldbody>.');
  }

  const links = [];
  const joints = [];
  const actuators = buildMujocoActuators(root);
  let visualCount = 0;
  let collisionCount = 0;

  async function visitBody(bodyNode, parentName = '', inheritedChildclass = '', parentWorld = new THREE.Matrix4()) {
    const linkName = bodyNode.attrs.name || `body_${links.length}`;
    // childclass propagates to this body's own geoms/joints and descendants
    // until overridden by a nearer childclass or an explicit element class.
    const childclass = bodyNode.attrs.childclass || inheritedChildclass;
    // Accumulate the body-chain world transform (this body relative to parent,
    // composed onto the parent's world frame).
    const bodyWorld = new THREE.Matrix4().copy(parentWorld).multiply(matrixFromPoseAttrs(bodyNode.attrs));
    const linkPayload = {
      name: linkName,
      inertial: parseMujocoInertial(bodyNode),
      visuals: [],
      collisions: []
    };

    let geomIndex = 0;
    for (const rawGeomNode of childElements(bodyNode, 'geom')) {
      // Resolve <default>/childclass inheritance so class-tagged attributes
      // (type/group/contype/...) are visible to classification and tessellation.
      const effAttrs = resolveElementAttrs(rawGeomNode, defaults.geom, defaults.rootGeom, childclass);
      const geomNode = { name: rawGeomNode.name, attrs: effAttrs, children: rawGeomNode.children || [] };
      const geomName = effAttrs.name || effAttrs.mesh || `${linkName}_geom_${geomIndex}`;
      // MuJoCo visibility is by geom group: 0-2 visible, 3-5 hidden/collision.
      // The resolved group (explicit on the geom, else from its class) is
      // authoritative and wins over the class NAME — e.g. iit_softfoot tags some
      // class="collision" meshes with group="0" to make them visible. No group =>
      // default group 0 (visible); class name is only a fallback hint.
      const geomClass = (effAttrs.class || '').toLowerCase();
      const geomGroup = Number(effAttrs.group);
      const isVisual = Number.isFinite(geomGroup) ? geomGroup < 3
        : (geomClass.includes('collision') || geomClass.includes('collider')) ? false
        : true;
      const geomWorld = new THREE.Matrix4().copy(bodyWorld).multiply(matrixFromPoseAttrs(geomNode.attrs));
      let payloads = null;
      if (!isVisual && !opts.tessellateCollisionShapes) {
        payloads = shapePayloadForMujocoGeom(geomNode, geomWorld, geomName, bodyWorld);
      }
      if (!payloads) {
        payloads = await mujocoGeomPayloads(geomNode, meshAssets, geomName, opts, bodyWorld);
      }
      if (isVisual) {
        linkPayload.visuals.push(...payloads.map((payload) => addMujocoPhysicsAttrs(payload, geomNode)));
        visualCount += payloads.length;
      } else {
        // Default approximation `convexHull` matches the convention in
        // `src/tydra/urdf-to-usd.cc::AddCollisionAPIs` and mirrors
        // NVIDIA / Newton's mujoco-usd-converter (see lightgeom
        // `doc/usd.md` "Mesh + collider convention"). Override per-geom
        // with `approximation: "none"` for triangle-soup MJCF colliders.
        for (const payload of payloads) {
          payload.approximation = payload.approximation || 'convexHull';
          addMujocoPhysicsAttrs(payload, geomNode);
        }
        linkPayload.collisions.push(...payloads);
        collisionCount += payloads.length;
      }
      geomIndex++;
    }

    links.push(linkPayload);

    if (parentName) {
      const jointNodes = childElements(bodyNode, 'joint');
      const jointNode = jointNodes[0] || null;
      // MuJoCo permits multiple <joint> per body; USD joints are pairwise, so we
      // represent the first and warn that the rest are dropped (matches urdf.js).
      if (jointNodes.length > 1) {
        console.warn(`MJCF body "${linkName}" has ${jointNodes.length} joints; only the `
          + `first is converted — ${jointNodes.length - 1} DOF(s) dropped.`);
      }
      if (jointNode) {
        const jAttrs = resolveElementAttrs(jointNode, defaults.joint, defaults.rootJoint, childclass);
        const axis = parseNumbers(jAttrs.axis, [0, 0, 1]);
        const range = parseNumbers(jAttrs.range, []);
        // The USD converter expects revolute limits in radians (it re-converts
        // to degrees). MJCF hinge ranges are in the compiler angle unit
        // (degrees by default); slide ranges are meters and pass through.
        const limScale = (jAttrs.type || 'hinge') === 'hinge' ? mjcfPoseCtx.toRad : 1;
        joints.push({
          name: jAttrs.name || `${parentName}_to_${linkName}`,
          type: mujocoJointType(jAttrs.type || 'hinge'),
          parent: parentName,
          child: linkName,
          axis,
          axisToken: axisToToken(axis),
          origin: parseNumbers(bodyNode.attrs.pos, [0, 0, 0]),
          originMatrix: matrixToUSDArray(matrixFromPoseAttrs(bodyNode.attrs)),
          limit: range.length >= 2 ? { lower: range[0] * limScale, upper: range[1] * limScale } : {},
          dynamics: {
            damping: numberAttr(jAttrs, 'damping'),
            friction: numberAttr(jAttrs, 'frictionloss')
          }
        });
      } else {
        joints.push({
          name: `${parentName}_to_${linkName}_fixed`,
          type: 'fixed',
          parent: parentName,
          child: linkName,
          axis: [1, 0, 0],
          axisToken: 'X',
          origin: parseNumbers(bodyNode.attrs.pos, [0, 0, 0]),
          originMatrix: matrixToUSDArray(matrixFromPoseAttrs(bodyNode.attrs)),
          limit: {},
          dynamics: {}
        });
      }
    }

    for (const childBody of childElements(bodyNode, 'body')) {
      await visitBody(childBody, linkName, childclass, bodyWorld);
    }
  }

  for (const bodyNode of childElements(worldbody, 'body')) {
    await visitBody(bodyNode);
  }

  return {
    payload: {
      name: root.attrs.model || path.basename(opts.inputFile || 'mujoco_scene', path.extname(opts.inputFile || 'mujoco_scene')),
      upAxis: opts.upAxis,
      gravity: opts.upAxis === 'Z' ? [0, 0, -1] : [0, -1, 0],
      timestep: numberAttr(firstChild(root, 'option')?.attrs, 'timestep'),
      links,
      joints,
      actuators
    },
    stats: {
      links: links.length,
      joints: joints.length,
      visuals: visualCount,
      collisions: collisionCount,
      actuators: actuators.length
    }
  };
}

async function buildExportPayload(urdfText, opts, urdfDir) {
  const metadata = parseURDFMetadata(urdfText);
  const resolver = new MeshResolver(opts, urdfDir);
  const links = [];
  let visualCount = 0;
  let collisionCount = 0;

  for (const linkInfo of metadata.links.values()) {
    const linkPayload = {
      name: linkInfo.name,
      inertial: linkInfo.inertial || {},
      visuals: [],
      collisions: []
    };

    let visualIndex = 0;
    for (const visualEl of findElements(linkInfo.body, 'visual')) {
      const geometryEl = firstElement(visualEl.body, 'geometry');
      if (!geometryEl) continue;
      const origin = originToMatrix(firstElement(visualEl.body, 'origin')?.attrs);
      const payloads = await meshPayloadsForGeometry(
        geometryEl,
        origin,
        resolver,
        visualEl.attrs.name || `${linkInfo.name}_visual_${visualIndex}`,
        opts
      );
      linkPayload.visuals.push(...payloads);
      visualCount += payloads.length;
      visualIndex++;
    }

    let collisionIndex = 0;
    for (const collisionEl of findElements(linkInfo.body, 'collision')) {
      const geometryEl = firstElement(collisionEl.body, 'geometry');
      if (!geometryEl) continue;
      const origin = originToMatrix(firstElement(collisionEl.body, 'origin')?.attrs);
      const fallbackName =
        collisionEl.attrs.name || `${linkInfo.name}_collision_${collisionIndex}`;
      let payloads = null;
      if (!opts.tessellateCollisionShapes) {
        payloads = shapePayloadForUrdfGeometry(geometryEl, origin, fallbackName);
      }
      if (!payloads) {
        payloads = await meshPayloadsForGeometry(
          geometryEl,
          origin,
          resolver,
          fallbackName,
          opts
        );
      }
      // URDF <collision> elements default to convex-hull approximation —
      // matches the writer convention in `src/tydra/urdf-to-usd.cc` and
      // mujoco-usd-converter. Override per-geom by authoring
      // `approximation` in the JSON payload.
      for (const payload of payloads) {
        payload.approximation = payload.approximation || 'convexHull';
      }
      linkPayload.collisions.push(...payloads);
      collisionCount += payloads.length;
      collisionIndex++;
    }

    links.push(linkPayload);
  }

  return {
    payload: {
      name: metadata.name || path.basename(opts.inputFile || 'sample', path.extname(opts.inputFile || 'sample')),
      upAxis: opts.upAxis,
      gravity: opts.upAxis === 'Z' ? [0, 0, -1] : [0, -1, 0],
      links,
      joints: metadata.joints,
      actuators: metadata.actuators
    },
    stats: {
      links: links.length,
      joints: metadata.joints.length,
      visuals: visualCount,
      collisions: collisionCount,
      actuators: metadata.actuators.length
    }
  };
}

async function buildPayload(inputText, opts, inputDir) {
  const isMJCF = opts.inputFormat === 'mjcf' ||
    (opts.inputFormat === 'auto' && /<mujoco\b/i.test(inputText));
  if (isMJCF) {
    return buildMujocoPayload(inputText, opts, inputDir);
  }
  return buildExportPayload(inputText, opts, inputDir);
}

function resolveOutputPath(inputFile, format, outputFile) {
  if (outputFile) {
    if (format === 'all') {
      const ext = path.extname(outputFile);
      return ext ? outputFile.slice(0, -ext.length) : outputFile;
    }
    return path.extname(outputFile) ? outputFile : `${outputFile}.${format}`;
  }
  const base = inputFile
    ? inputFile.replace(/\.(urdf|xml)$/i, '')
    : path.resolve(process.cwd(), 'sample-urdf');
  return format === 'all' ? base : `${base}.${format}`;
}

function writeExport(native, format, outPath) {
  if (format === 'usda') {
    const text = native.exportAsUSDA();
    if (!text) throw new Error(native.error() || 'USDA export failed');
    fs.writeFileSync(outPath, text, 'utf8');
    return text.length;
  }
  if (format === 'usdc') {
    const data = native.exportAsUSDC();
    if (!data) throw new Error(native.error() || 'USDC export failed');
    const bytes = new Uint8Array(data);
    fs.writeFileSync(outPath, bytes);
    return bytes.length;
  }
  const data = native.exportAsUSDZ();
  if (!data) throw new Error(native.error() || 'USDZ export failed');
  const bytes = new Uint8Array(data);
  fs.writeFileSync(outPath, bytes);
  return bytes.length;
}

function verifyUSDA(native, stats) {
  const usda = native.exportAsUSDA();
  if (!usda) throw new Error(native.error() || 'USDA verification export failed');
  const required = [
    'PhysicsScene',
    'MjcSceneAPI',
    'NewtonSceneAPI'
  ];
  if (stats.links > 0) {
    required.push('PhysicsRigidBodyAPI');
    required.push('NewtonArticulationRootAPI');
  }
  if (stats.collisions > 0) {
    required.push('PhysicsCollisionAPI');
    required.push('MjcCollisionAPI');
    required.push('NewtonCollisionAPI');
    // The convention in `src/tydra/urdf-to-usd.cc::AddCollisionAPIs`
    // also stamps `MjcImageableAPI` + `purpose = "guide"` on every
    // collider so default Hydra renders skip them.
    required.push('MjcImageableAPI');
    required.push('uniform token purpose = "guide"');
  }
  if (stats.joints > 0) required.push('MjcJointAPI');
  if ((stats.actuators || 0) > 0) required.push('NewtonActuator');
  const missing = required.filter((token) => !usda.includes(token));
  if (missing.length) {
    throw new Error(`Verification failed. Missing USDA tokens: ${missing.join(', ')}`);
  }
}

async function main() {
  const opts = parseArgs();
  const inputPath = opts.sample ? null : path.resolve(opts.inputFile);
  const urdfDir = inputPath ? path.dirname(inputPath) : process.cwd();
  const urdfText = opts.sample ? SAMPLE_URDF : fs.readFileSync(inputPath, 'utf8');

  if (inputPath && !opts.assetDirs.length) {
    opts.assetDirs.push(urdfDir);
  }

  const { payload, stats } = await buildPayload(urdfText, opts, urdfDir);
  if (opts.dumpJson) {
    fs.mkdirSync(path.dirname(path.resolve(opts.dumpJson)), { recursive: true });
    fs.writeFileSync(opts.dumpJson, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
  }

  const tinyusdz = await TinyUSDZFactory();
  const native = new tinyusdz.TinyUSDZLoaderNative();
  let payloadJSON;
  try {
    payloadJSON = JSON.stringify(payload);
  } catch (err) {
    if (err instanceof RangeError) {
      throw new Error(
        `Payload too large to marshal: tessellated mesh geometry exceeds V8's max string length ` +
        `(~512M chars). Re-run on a lighter model, or reduce mesh density ` +
        `(${stats.visuals} visual meshes, ${stats.collisions} collisions).`
      );
    }
    throw err;
  }

  // Register tessellated mesh geometry as binary typed arrays (referenced by
  // meshRef in the payload) before authoring the scene.
  if (native.clearURDFMeshBuffers) native.clearURDFMeshBuffers();
  for (const [ref, buf] of meshBuffers) {
    if (!native.setVisualMesh(ref, buf.positions, buf.normals, buf.uvs, buf.indices)) {
      throw new Error(native.error() || `Failed to register mesh buffer "${ref}"`);
    }
  }

  const created = native.createURDFPhysicsScene(payloadJSON);
  if (!created) {
    throw new Error(native.error() || 'createURDFPhysicsScene failed');
  }
  const warn = native.warn?.();
  if (warn) console.warn(warn.trim());

  // Raise the USDC writer's conservative WASM size caps when requested, so
  // mesh-dense scenes can export past the 100MB default.
  if ((opts.maxUsdcMb > 0 || opts.maxMemMb > 0) && native.setUSDCExportLimitMB) {
    native.setUSDCExportLimitMB(opts.maxUsdcMb, opts.maxMemMb);
  }

  if (opts.verify) verifyUSDA(native, stats);

  const formats = opts.format === 'all' ? ['usda', 'usdc', 'usdz'] : [opts.format];
  const basePath = resolveOutputPath(inputPath, opts.format, opts.outputFile);
  for (const format of formats) {
    const outPath = opts.format === 'all' ? `${basePath}.${format}` : basePath;
    fs.mkdirSync(path.dirname(path.resolve(outPath)), { recursive: true });
    const bytes = writeExport(native, format, outPath);
    console.log(`Wrote ${outPath} (${bytes} ${format === 'usda' ? 'chars' : 'bytes'})`);
  }
  native.delete();

  console.log(`Verified ${payload.name}: ${stats.links} links, ${stats.joints} joints, ${stats.visuals} visual meshes, ${stats.collisions} collisions, ${stats.actuators || 0} Newton actuators.`);
  if (opts.verbose) {
    console.log(`Formats: ${formats.join(', ')}`);
    if (opts.dumpJson) console.log(`Payload JSON: ${opts.dumpJson}`);
  }
}

main().catch((err) => {
  console.error(`urdf-to-usd: ${err.message}`);
  if (process.argv.includes('--verbose') || process.argv.includes('-v')) {
    console.error(err.stack);
  }
  process.exit(1);
});
