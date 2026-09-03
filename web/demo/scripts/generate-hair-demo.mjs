#!/usr/bin/env node
import { mkdtemp, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const demoRoot = path.resolve(here, '..');
const repoRoot = path.resolve(demoRoot, '../..');
const output = path.join(demoRoot, 'public/assets/hair-fur-demo.usdc');
const checkOnly = process.argv.includes('--check');
const minBytes = 700 * 1024;
const maxBytes = 1400 * 1024;

function fmt(value) { return Number(value).toFixed(5).replace(/\.?0+$/, ''); }
function randomFactory(seed) {
  let state = seed >>> 0;
  return () => ((state = Math.imul(state ^ state >>> 15, 1 | state),
    state ^= state + Math.imul(state ^ state >>> 7, 61 | state),
    ((state ^ state >>> 14) >>> 0) / 4294967296));
}
function curvesBlock(name, material, strands, segments, pointAt, width) {
  const counts = [], points = [], widths = [], colors = [];
  for (let strand = 0; strand < strands; strand++) {
    counts.push(segments + 1);
    const colorJitter = ((strand * 16807) % 97) / 96;
    for (let i = 0; i <= segments; i++) {
      const u = i / segments;
      const p = pointAt(strand, u);
      points.push(`(${fmt(p[0])},${fmt(p[1])},${fmt(p[2])})`);
      widths.push(fmt(width * (1 - 0.92 * u)));
      colors.push(`(${fmt(0.82 + colorJitter * 0.18)},${fmt(0.87 + colorJitter * 0.13)},1)`);
    }
  }
  return `def BasisCurves "${name}" {
    uniform token type = "cubic"
    uniform token basis = "bspline"
    uniform token wrap = "nonperiodic"
    int[] curveVertexCounts = [${counts.join(',')}]
    point3f[] points = [${points.join(',')}]
    float[] widths = [${widths.join(',')}] (interpolation = "vertex")
    color3f[] primvars:displayColor = [${colors.join(',')}] (interpolation = "vertex")
    rel material:binding = </HairFur/Looks/${material}>
  }`;
}
function materialBlock(name, colors, absorption, roughness) {
  return `def Material "${name}" {
    token outputs:mtlx:surface.connect = </HairFur/Looks/${name}/Hair.outputs:out>
    def Shader "Hair" {
      uniform token info:id = "ND_chiang_hair_bsdf"
      color3f inputs:tint_R = (${colors[0].join(',')})
      color3f inputs:tint_TT = (${colors[1].join(',')})
      color3f inputs:tint_TRT = (${colors[2].join(',')})
      float2 inputs:roughness_R = (${roughness}, ${roughness * 1.35})
      float2 inputs:roughness_TT = (${roughness * 1.35}, ${roughness * 1.7})
      float2 inputs:roughness_TRT = (${roughness * 1.8}, ${roughness * 2.1})
      color3f inputs:absorption_coefficient = (${absorption.join(',')})
      float inputs:ior = 1.55
      float inputs:cuticle_angle = 3
      token outputs:out
    }
  }`;
}
function makeUSDA() {
  const random = randomFactory(20260828);
  const furStrands = 3250, grassStrands = 3750, segments = 5;
  const fur = curvesBlock('FurballCurves', 'FurHair', furStrands, segments, (strand, u) => {
    const y = 1 - 2 * ((strand + 0.5) / furStrands);
    const r = Math.sqrt(Math.max(0, 1 - y * y));
    const phi = strand * Math.PI * (3 - Math.sqrt(5));
    const nx = Math.cos(phi) * r, nz = Math.sin(phi) * r;
    const length = 0.22 + (((strand * 48271) % 997) / 997) * 0.14;
    const curl = ((((strand * 69621) % 991) / 991) - 0.5) * 0.1;
    return [-1.7 + nx * (1 + length * u) + curl * u * u * Math.sin(phi),
      1.18 + y * (1 + length * u) + 0.03 * u * u,
      nz * (1 + length * u) - curl * u * u * Math.cos(phi)];
  }, 0.018);
  const side = Math.ceil(Math.sqrt(grassStrands));
  const grassSeeds = Array.from({ length: grassStrands }, () => [random(), random(), random(), random()]);
  const grass = curvesBlock('GrassCurves', 'GrassHair', grassStrands, segments, (strand, u) => {
    const gx = strand % side, gz = Math.floor(strand / side), s = grassSeeds[strand];
    const x = 1.6 + (gx / side - 0.5) * 3.2 + (s[0] - 0.5) * 0.045;
    const z = (gz / side - 0.5) * 3.2 + (s[1] - 0.5) * 0.045;
    const h = 0.24 + s[2] * 0.42, bend = (s[3] - 0.5) * 0.34;
    return [x + bend * u * u, h * u, z + bend * 0.27 * u * u];
  }, 0.02);
  return `#usda 1.0
(
  defaultPrim = "HairFur"
  upAxis = "Y"
  metersPerUnit = 1
)
def Xform "HairFur" {
  def Scope "Looks" {
    ${materialBlock('FurHair', [[0.46,0.13,0.035],[0.28,0.055,0.012],[0.12,0.025,0.006]], [0.35,0.8,1.4], 0.22)}
    ${materialBlock('GrassHair', [[0.12,0.34,0.035],[0.08,0.22,0.025],[0.18,0.3,0.04]], [0.7,0.15,1.1], 0.3)}
  }
  def Sphere "FurballCore" {
    double radius = 1
    double3 xformOp:translate = (-1.7, 1.18, 0)
    uniform token[] xformOpOrder = ["xformOp:translate"]
  }
  def Cube "Ground" {
    double size = 2
    double3 xformOp:scale = (1.7, 0.025, 1.7)
    double3 xformOp:translate = (1.6, -0.035, 0)
    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]
  }
  ${fur}
  ${grass}
}`;
}

async function validate() {
  const info = await stat(output).catch(() => null);
  if (!info) throw new Error(`Missing ${path.relative(repoRoot, output)}; run npm run generate:hair-demo`);
  if (info.size < minBytes || info.size > maxBytes) {
    throw new Error(`Hair sample is ${(info.size / 1024).toFixed(0)} KiB; expected 700–1400 KiB`);
  }
  console.log(`hair-fur-demo.usdc: ${(info.size / 1024).toFixed(0)} KiB`);
}
if (checkOnly) {
  await validate();
} else {
  const temp = await mkdtemp(path.join(tmpdir(), 'lightusd-hair-'));
  try {
    const source = path.join(temp, 'hair-fur-demo.usda');
    await writeFile(source, makeUSDA());
    const tusdcat = process.env.TUSDCAT || path.join(repoRoot, 'build_ninja/tusdcat');
    const result = spawnSync(tusdcat, ['--output-format', 'usdc', '--compress-float-arrays', '-o', output, source], { stdio: 'inherit' });
    if (result.status !== 0) throw new Error(`${tusdcat} failed with exit code ${result.status}`);
    await validate();
  } finally { await rm(temp, { recursive: true, force: true }); }
}
