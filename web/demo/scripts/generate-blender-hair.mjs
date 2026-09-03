#!/usr/bin/env node
import { mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const demoRoot = path.resolve(here, '..');
const repoRoot = path.resolve(demoRoot, '../..');
const blender = process.env.BLENDER || '/mnt/nvme02/local/blender-5.2.1-linux-x64/blender';
const output = path.join(demoRoot, 'public/assets/blender-hair-curves.usdc');
const blend = process.env.HAIR_BLEND_OUTPUT || path.join('/tmp', 'lightusd-hair-curves.blend');
const tusdcat = process.env.TUSDCAT || path.join(repoRoot, 'build_ninja/tusdcat');
const checkOnly = process.argv.includes('--check');

async function check() {
  const info = await stat(output).catch(() => null);
  if (!info) throw new Error('Missing blender-hair-curves.usdc; run npm run generate:blender-hair');
  if (info.size < 3 * 1024 * 1024 || info.size > 12 * 1024 * 1024) {
    throw new Error(`Blender hair asset is ${(info.size / 1048576).toFixed(2)} MiB; expected 3–12 MiB`);
  }
  console.log(`blender-hair-curves.usdc: ${(info.size / 1048576).toFixed(2)} MiB, 10,000 strands`);
}

if (!checkOnly) {
  const temp = await mkdtemp(path.join(tmpdir(), 'lightusd-blender-hair-'));
  try {
    const nativeUsd = path.join(temp, 'blender-native.usda');
    const script = path.join(here, 'blender-generate-hair.py');
    const result = spawnSync(blender, ['--factory-startup', '-b', '--python', script, '--', nativeUsd, blend], {
      cwd: repoRoot, stdio: 'inherit', timeout: 15000,
      env: { ...process.env, BLENDER_USER_CONFIG: path.join(temp, 'config') }
    });
    // Some headless builds linger while shutting down their audio/main-loop
    // threads in a restricted container. The export itself is complete once
    // the native file exists, so a post-export timeout is safe here.
    if (result.status !== 0 && !existsSync(nativeUsd)) {
      throw new Error(`Blender failed with exit code ${result.status}`);
    }
    // Blender 5.2 exports Hair Curves and their material bindings, but its
    // MaterialX bridge reports Principled Hair as unsupported. Patch the two
    // otherwise-empty Material definitions before the final USDA -> USDC pass.
    let usda = await readFile(nativeUsd, 'utf8');
    function attachHair(materialName, body) {
      const marker = `def Material "${materialName}"`;
      const start = usda.indexOf(marker);
      if (start < 0) throw new Error(`Blender USD omitted ${materialName}`);
      const open = usda.indexOf('{', start);
      const close = usda.indexOf('}', open);
      if (open < 0 || close < 0) throw new Error(`Malformed Blender material ${materialName}`);
      usda = `${usda.slice(0, open + 1)}\n${body}\n${usda.slice(close)}`;
    }
    attachHair('Straight_PrincipledHair', `      token outputs:mtlx:surface.connect = </root/_materials/Straight_PrincipledHair/Hair.outputs:out>
      def Shader "Hair" {
        uniform token info:id = "ND_chiang_hair_bsdf"
        color3f inputs:tint_R = (0.16, 0.035, 0.012)
        color3f inputs:tint_TT = (0.11, 0.02, 0.006)
        color3f inputs:tint_TRT = (0.055, 0.012, 0.003)
        float2 inputs:roughness_R = (0.22, 0.34)
        float2 inputs:roughness_TT = (0.30, 0.43)
        float2 inputs:roughness_TRT = (0.40, 0.52)
        color3f inputs:absorption_coefficient = (0.25, 0.9, 1.65)
        float inputs:ior = 1.55
        float inputs:cuticle_angle = 3
        token outputs:out
      }
`);
    attachHair('Wavy_PrincipledHair', `      token outputs:mtlx:surface.connect = </root/_materials/Wavy_PrincipledHair/Hair.outputs:out>
      def Shader "Hair" {
        uniform token info:id = "ND_chiang_hair_bsdf"
        color3f inputs:tint_R = (0.42, 0.15, 0.035)
        color3f inputs:tint_TT = (0.28, 0.075, 0.015)
        color3f inputs:tint_TRT = (0.13, 0.035, 0.006)
        float2 inputs:roughness_R = (0.29, 0.43)
        float2 inputs:roughness_TT = (0.39, 0.53)
        float2 inputs:roughness_TRT = (0.49, 0.62)
        color3f inputs:absorption_coefficient = (0.35, 0.72, 1.45)
        float inputs:ior = 1.55
        float inputs:cuticle_angle = 3
        token outputs:out
      }
`);
    await writeFile(nativeUsd, usda);
    const flatten = spawnSync(tusdcat, ['--output-format', 'usdc', '--compress-float-arrays',
      '-o', output, nativeUsd], { cwd: repoRoot, stdio: 'inherit' });
    if (flatten.status !== 0) throw new Error(`MaterialX export hook failed with exit code ${flatten.status}`);
  } finally {
    await rm(temp, { recursive: true, force: true });
  }
}
await check();
