import * as THREE from 'three';
import {
  linearColorTransformMatrix,
  transformLinearColor
} from './ColorSpaceUtils.js';

export { linearColorTransformMatrix, transformLinearColor };

// Linear ACES2065-1/AP0 values from OpenColorIO's ColorChecker24 CPU test
// (tests/cpu/ops/fixedfunction/FixedFunctionOpCPU_tests.cpp). Keeping the
// source values avoids baking a display transform into the reference chart.
export const MACBETH_COLORCHECKER_AP0 = Object.freeze([
  [0.11877, 0.08709, 0.05895], [0.40002, 0.31916, 0.23736],
  [0.18476, 0.20398, 0.31311], [0.10901, 0.13511, 0.06493],
  [0.26684, 0.24604, 0.40932], [0.32283, 0.46208, 0.40606],
  [0.38605, 0.22743, 0.05777], [0.13822, 0.13037, 0.33703],
  [0.30202, 0.13752, 0.12758], [0.09310, 0.06347, 0.13525],
  [0.34876, 0.43654, 0.10613], [0.48655, 0.36685, 0.08061],
  [0.08732, 0.07443, 0.27274], [0.15366, 0.25692, 0.09071],
  [0.21742, 0.07070, 0.05130], [0.58919, 0.53943, 0.09157],
  [0.30904, 0.14818, 0.27426], [0.14901, 0.23378, 0.35939],
  [0.86653, 0.86792, 0.85818], [0.57356, 0.57256, 0.57169],
  [0.35346, 0.35337, 0.35391], [0.20253, 0.20243, 0.20287],
  [0.09467, 0.09520, 0.09637], [0.03745, 0.03766, 0.03895]
]);

function mulv(m, v) {
  return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
    m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
    m[6] * v[0] + m[7] * v[1] + m[8] * v[2]];
}

export function createMacbethColorChart({ patchSize = 0.28, gap = 0.025,
  sourceColorSpace = 'lin_ap0_scene', name = 'MacbethColorChecker24' } = {}) {
  const group = new THREE.Group();
  group.name = name;
  const width = 6 * patchSize + 7 * gap;
  const height = 4 * patchSize + 5 * gap;
  const backing = new THREE.Mesh(new THREE.PlaneGeometry(width, height),
    new THREE.MeshBasicMaterial({ color: 0.015, side: THREE.DoubleSide }));
  backing.position.z = -0.002;
  group.add(backing);
  const geometry = new THREE.PlaneGeometry(patchSize, patchSize);
  const matrix = linearColorTransformMatrix(sourceColorSpace,
    'lin_rec709_scene');
  const samplePoints = [];
  MACBETH_COLORCHECKER_AP0.forEach((reference, index) => {
    const row = Math.floor(index / 6);
    const column = index % 6;
    const displayLinear = mulv(matrix, reference);
    const material = new THREE.MeshBasicMaterial({
      color: new THREE.Color(displayLinear[0], displayLinear[1], displayLinear[2]),
      toneMapped: false,
      side: THREE.DoubleSide
    });
    const patch = new THREE.Mesh(geometry, material);
    patch.name = `ColorCheckerPatch${String(index + 1).padStart(2, '0')}`;
    patch.position.set((column - 2.5) * (patchSize + gap),
      (1.5 - row) * (patchSize + gap), 0);
    patch.userData.referenceLinearAP0 = [...reference];
    patch.userData.expectedLinearRec709 = displayLinear;
    patch.userData.patchIndex = index;
    group.add(patch);
    samplePoints.push({ name: patch.name, object: patch,
      expectedLinear: displayLinear });
  });
  group.userData.sourceColorSpace = sourceColorSpace;
  group.userData.samplePoints = samplePoints;
  return group;
}

export function createGrayAndChromeBalls({ radius = 0.42,
  separation = 1.05 } = {}) {
  const group = new THREE.Group();
  group.name = 'GrayAndChromeBalls';
  const geometry = new THREE.SphereGeometry(radius, 64, 32);
  const gray = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({
    color: new THREE.Color(0.18, 0.18, 0.18), metalness: 0, roughness: 0.5
  }));
  gray.name = 'GrayBall18Percent';
  gray.position.x = -separation * 0.5;
  const chrome = new THREE.Mesh(geometry, new THREE.MeshStandardMaterial({
    color: 1, metalness: 1, roughness: 0.015
  }));
  chrome.name = 'ChromeMirrorBall';
  chrome.position.x = separation * 0.5;
  group.add(gray, chrome);
  return group;
}

export function addColorCalibrationScene(scene, options = {}) {
  const root = new THREE.Group();
  root.name = options.name || 'ColorCalibrationTestKit';
  const chart = createMacbethColorChart(options.chart);
  const balls = createGrayAndChromeBalls(options.balls);
  balls.position.set(0, options.ballsY ?? -1.15, options.ballsZ ?? 0.25);
  root.add(chart, balls);
  scene.add(root);
  return { root, chart, balls, samples: chart.userData.samplePoints };
}

export function comparePixelSamples(actual, expected, tolerance = 0.025) {
  const failures = [];
  const count = Math.min(actual.length, expected.length);
  for (let i = 0; i < count; ++i) {
    const delta = Math.max(Math.abs(actual[i][0] - expected[i][0]),
      Math.abs(actual[i][1] - expected[i][1]),
      Math.abs(actual[i][2] - expected[i][2]));
    if (delta > tolerance) failures.push({ index: i, actual: actual[i],
      expected: expected[i], delta });
  }
  return { pass: failures.length === 0 && actual.length === expected.length,
    failures, tolerance };
}

// Float readback is preferred. Unsigned-byte fallback still validates the
// numeric pipeline and is intentionally independent of display-p3 hardware.
export function readRenderTargetPixels(renderer, target, points) {
  const width = target.width, height = target.height;
  const floatReadback = target.texture.type === THREE.FloatType;
  const pixel = floatReadback ? new Float32Array(4) : new Uint8Array(4);
  return points.map(({ x, y }) => {
    renderer.readRenderTargetPixels(target,
      Math.max(0, Math.min(width - 1, Math.round(x))),
      Math.max(0, Math.min(height - 1, Math.round(y))), 1, 1, pixel);
    const scale = floatReadback ? 1 : 1 / 255;
    return [pixel[0] * scale, pixel[1] * scale, pixel[2] * scale, pixel[3] * scale];
  });
}
