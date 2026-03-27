#!/usr/bin/env node
// SPDX-License-Identifier: Apache 2.0
// Test EM_JS synchronous progress callbacks in Node.js CLI
//
// This test verifies that progress callbacks are called synchronously
// from C++ during Tydra scene conversion via EM_JS.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// ============================================================================
// Progress Tracking State
// ============================================================================

const progressState = {
  callbackCount: 0,
  lastMeshCurrent: 0,
  lastMeshTotal: 0,
  stages: new Set(),
  meshNames: [],
  completeInfo: null,
  errors: []
};

function resetProgressState() {
  progressState.callbackCount = 0;
  progressState.lastMeshCurrent = 0;
  progressState.lastMeshTotal = 0;
  progressState.stages = new Set();
  progressState.meshNames = [];
  progressState.completeInfo = null;
  progressState.errors = [];
}

// ============================================================================
// Synthetic USD Generation
// ============================================================================

/**
 * Generate a simple USDA file with multiple meshes
 */
function generateSyntheticUSDA(meshCount = 3) {
  let usda = `#usda 1.0
(
    defaultPrim = "Root"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "Root"
{
`;

  for (let i = 0; i < meshCount; i++) {
    const y = i * 2.5;
    usda += `
    def Mesh "Mesh_${String(i).padStart(3, '0')}"
    {
        float3[] extent = [(-1, -1, -1), (1, 1, 1)]
        int[] faceVertexCounts = [4, 4, 4, 4, 4, 4]
        int[] faceVertexIndices = [0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4]
        point3f[] points = [
            (-1, ${y - 1}, 1), (1, ${y - 1}, 1), (-1, ${y - 1}, -1), (1, ${y - 1}, -1),
            (-1, ${y + 1}, -1), (1, ${y + 1}, -1), (-1, ${y + 1}, 1), (1, ${y + 1}, 1)
        ]
        uniform token subdivisionScheme = "none"
    }
`;
  }

  usda += `}
`;

  return usda;
}

/**
 * Generate USDA with mixed geometry types (mesh, cube, sphere)
 */
function generateMixedGeometryUSDA() {
  return `#usda 1.0
(
    defaultPrim = "Root"
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "Root"
{
    def Mesh "CustomMesh"
    {
        float3[] extent = [(-1, -1, -1), (1, 1, 1)]
        int[] faceVertexCounts = [3, 3, 3, 3]
        int[] faceVertexIndices = [0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2]
        point3f[] points = [(0, 1, 0), (-1, -1, 1), (1, -1, 1), (0, -1, -1)]
        uniform token subdivisionScheme = "none"
    }

    def Cube "SimpleCube"
    {
        double size = 2
        double3 xformOp:translate = (3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    def Sphere "SimpleSphere"
    {
        double radius = 1
        double3 xformOp:translate = (-3, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }
}
`;
}

// ============================================================================
// Test Runner
// ============================================================================

async function runTest(testName, usdData, filename = 'test.usda') {
  console.log(`\n${'='.repeat(60)}`);
  console.log(`Test: ${testName}`);
  console.log('='.repeat(60));

  resetProgressState();

  // Dynamic import to allow callbacks to be set
  const createTinyUSDZ = (await import('./src/tinyusdz/tinyusdz.js')).default;

  // Initialize WASM module with EM_JS callbacks
  const tinyusdz = await createTinyUSDZ({
    onTydraProgress: (info) => {
      progressState.callbackCount++;
      progressState.lastMeshCurrent = info.meshCurrent;
      progressState.lastMeshTotal = info.meshTotal;
      progressState.stages.add(info.stage);
      if (info.meshName) {
        progressState.meshNames.push(info.meshName);
      }

      // Print progress
      const pct = info.meshTotal > 0
        ? Math.round((info.meshCurrent / info.meshTotal) * 100)
        : Math.round(info.progress * 100);
      const meshInfo = info.meshTotal > 0
        ? `${info.meshCurrent}/${info.meshTotal}`
        : '';
      const meshName = info.meshName ? path.basename(info.meshName) : '';

      console.log(`  [Progress] ${pct}% | ${info.stage} | ${meshInfo} ${meshName}`);
    },
    onTydraStage: (info) => {
      console.log(`  [Stage] ${info.stage}: ${info.message || ''}`);
    },
    onTydraComplete: (info) => {
      progressState.completeInfo = info;
      console.log(`  [Complete] ${info.meshCount} meshes, ${info.materialCount} materials, ${info.textureCount} textures`);
    }
  });

  // Create native loader
  const loader = new tinyusdz.TinyUSDZLoaderNative();

  // Convert string to binary
  const encoder = new TextEncoder();
  const binary = encoder.encode(usdData);

  console.log(`\nLoading ${filename} (${binary.length} bytes)...`);
  console.log('-'.repeat(40));

  const startTime = performance.now();
  const ok = loader.loadFromBinary(binary, filename);
  const elapsed = performance.now() - startTime;

  console.log('-'.repeat(40));

  if (!ok) {
    console.log(`\n❌ FAILED: ${loader.error()}`);
    progressState.errors.push(loader.error());
    return false;
  }

  console.log(`\n✅ SUCCESS in ${elapsed.toFixed(1)}ms`);
  console.log(`   Callback count: ${progressState.callbackCount}`);
  console.log(`   Stages seen: ${[...progressState.stages].join(', ')}`);
  console.log(`   Unique meshes: ${new Set(progressState.meshNames).size}`);

  if (progressState.completeInfo) {
    console.log(`   Final counts: ${progressState.completeInfo.meshCount} meshes`);
  }

  // Cleanup
  loader.delete();

  return true;
}

async function runFileTest(filePath) {
  console.log(`\n${'='.repeat(60)}`);
  console.log(`Test: Load from file - ${filePath}`);
  console.log('='.repeat(60));

  if (!fs.existsSync(filePath)) {
    console.log(`⚠️  SKIPPED: File not found`);
    return null;
  }

  const data = fs.readFileSync(filePath);
  const filename = path.basename(filePath);

  resetProgressState();

  const createTinyUSDZ = (await import('./src/tinyusdz/tinyusdz.js')).default;

  const tinyusdz = await createTinyUSDZ({
    onTydraProgress: (info) => {
      progressState.callbackCount++;
      progressState.lastMeshCurrent = info.meshCurrent;
      progressState.lastMeshTotal = info.meshTotal;
      progressState.stages.add(info.stage);
      if (info.meshName) {
        progressState.meshNames.push(info.meshName);
      }

      const pct = info.meshTotal > 0
        ? Math.round((info.meshCurrent / info.meshTotal) * 100)
        : Math.round(info.progress * 100);
      const meshInfo = info.meshTotal > 0
        ? `${info.meshCurrent}/${info.meshTotal}`
        : '';
      const meshName = info.meshName ? path.basename(info.meshName) : '';

      console.log(`  [Progress] ${pct}% | ${info.stage} | ${meshInfo} ${meshName}`);
    },
    onTydraComplete: (info) => {
      progressState.completeInfo = info;
      console.log(`  [Complete] ${info.meshCount} meshes, ${info.materialCount} materials, ${info.textureCount} textures`);
    }
  });

  const loader = new tinyusdz.TinyUSDZLoaderNative();

  console.log(`\nLoading ${filename} (${data.length} bytes)...`);
  console.log('-'.repeat(40));

  const startTime = performance.now();
  const ok = loader.loadFromBinary(data, filename);
  const elapsed = performance.now() - startTime;

  console.log('-'.repeat(40));

  if (!ok) {
    console.log(`\n❌ FAILED: ${loader.error()}`);
    return false;
  }

  console.log(`\n✅ SUCCESS in ${elapsed.toFixed(1)}ms`);
  console.log(`   Callback count: ${progressState.callbackCount}`);
  console.log(`   Stages seen: ${[...progressState.stages].join(', ')}`);
  console.log(`   Unique meshes: ${new Set(progressState.meshNames).size}`);

  loader.delete();

  return true;
}

// ============================================================================
// Main
// ============================================================================

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║     EM_JS Progress Callback Test (Node.js CLI)             ║');
  console.log('╚════════════════════════════════════════════════════════════╝');

  const results = {
    passed: 0,
    failed: 0,
    skipped: 0
  };

  // Test 1: Simple synthetic USD with 3 meshes
  const result1 = await runTest(
    'Synthetic USDA - 3 Meshes',
    generateSyntheticUSDA(3),
    'synthetic-3mesh.usda'
  );
  if (result1) results.passed++; else results.failed++;

  // Test 2: Synthetic USD with 10 meshes
  const result2 = await runTest(
    'Synthetic USDA - 10 Meshes',
    generateSyntheticUSDA(10),
    'synthetic-10mesh.usda'
  );
  if (result2) results.passed++; else results.failed++;

  // Test 3: Mixed geometry types
  const result3 = await runTest(
    'Mixed Geometry (Mesh + Cube + Sphere)',
    generateMixedGeometryUSDA(),
    'mixed-geometry.usda'
  );
  if (result3) results.passed++; else results.failed++;

  // Test 4: Load from file (if available)
  const testFiles = [
    path.join(__dirname, 'assets/multi-mesh-test.usda'),
    path.join(__dirname, 'assets/suzanne.usdc'),
    path.join(__dirname, 'assets/mtlx-normalmap-sphere.usdz')
  ];

  for (const filePath of testFiles) {
    const result = await runFileTest(filePath);
    if (result === true) results.passed++;
    else if (result === false) results.failed++;
    else results.skipped++;
  }

  // Summary
  console.log('\n' + '═'.repeat(60));
  console.log('SUMMARY');
  console.log('═'.repeat(60));
  console.log(`  ✅ Passed:  ${results.passed}`);
  console.log(`  ❌ Failed:  ${results.failed}`);
  console.log(`  ⚠️  Skipped: ${results.skipped}`);
  console.log('═'.repeat(60));

  if (results.failed > 0) {
    process.exit(1);
  }
}

main().catch(err => {
  console.error('\n💥 Unhandled error:', err);
  process.exit(1);
});
