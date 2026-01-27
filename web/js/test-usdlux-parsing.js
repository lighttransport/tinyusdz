#!/usr/bin/env node
// UsdLux Parsing and Conversion Test Suite
// Tests Node.js WASM bindings for UsdLux light parsing

import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';
import fs from 'node:fs';
import path from 'node:path';

// Test file configurations
const testFiles = [
  {
    name: 'Basic Lights',
    file: '../../tests/usda/usdlux_basic_lights.usda',
    expectedLights: 5,
    description: 'Point, Directional, Rect, Disk, Cylinder, Dome lights'
  },
  {
    name: 'Advanced Features',
    file: '../../tests/usda/usdlux_advanced_features.usda',
    expectedLights: 4,
    description: 'IES profiles, shaping, textured lights'
  },
  {
    name: 'Mesh Lights',
    file: '../../tests/usda/usdlux_mesh_lights_simple.usda',
    expectedLights: 1, // Only StandardLight - mesh lights are in meshes array now
    expectedAreaLightMeshes: 2, // EmissiveCube and LightPanel with MeshLightAPI
    description: 'MeshLightAPI geometry lights (Option B: meshes with area light flags)'
  },
  {
    name: 'Complete Scene',
    file: '../../tests/feat/lux/04_complete_scene.usda',
    expectedLights: 2,
    description: 'Three-point lighting with materials'
  }
];

// Test result tracking
const results = {
  passed: 0,
  failed: 0,
  tests: []
};

function loadFile(filename) {
  try {
    const data = fs.readFileSync(filename);
    const mimeType = 'application/octet-stream';
    const blob = new Blob([data], { type: mimeType });
    const f = new File([blob], path.basename(filename), { type: blob.type });
    return { file: f, arrayBuffer: data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) };
  } catch (err) {
    return null;
  }
}

async function testFile(testConfig) {
  const result = {
    name: testConfig.name,
    file: testConfig.file,
    passed: false,
    error: null,
    numLights: 0,
    lights: [],
    numAreaLightMeshes: 0,
    areaLightMeshes: []
  };

  try {
    console.log(`\n${'='.repeat(60)}`);
    console.log(`Testing: ${testConfig.name}`);
    console.log(`File: ${testConfig.file}`);
    console.log(`Expected: ${testConfig.expectedLights} lights`);
    console.log(`Description: ${testConfig.description}`);
    console.log('='.repeat(60));

    // Check if file exists
    if (!fs.existsSync(testConfig.file)) {
      throw new Error(`File not found: ${testConfig.file}`);
    }

    // Initialize loader
    const loader = new TinyUSDZLoader();
    await loader.init({ useMemory64: false });
    loader.setMaxMemoryLimitMB(500);

    // Load file
    const fileData = loadFile(testConfig.file);
    if (!fileData) {
      throw new Error('Failed to load file');
    }

    // Parse USD file
    const usd = await new Promise((resolve, reject) => {
      loader.parse(fileData.arrayBuffer, testConfig.file, resolve, reject);
    });

    if (!usd) {
      throw new Error('Failed to parse USD file');
    }

    // Get light count
    const numLights = usd.numLights();
    result.numLights = numLights;

    console.log(`\n✓ Loaded successfully`);
    console.log(`  Scene info:`);
    console.log(`    - Meshes: ${usd.numMeshes()}`);
    console.log(`    - Materials: ${usd.numMaterials()}`);
    console.log(`    - Lights: ${numLights}`);

    // Parse each light
    console.log(`\n  Light details:`);
    for (let i = 0; i < numLights; i++) {
      const lightResult = usd.getLightWithFormat(i, 'json');

      if (lightResult.error) {
        console.log(`    ✗ Light ${i}: ERROR - ${lightResult.error}`);
        continue;
      }

      const light = JSON.parse(lightResult.data);
      result.lights.push(light);

      console.log(`    ✓ Light ${i}: ${light.name}`);
      console.log(`        Type: ${light.type || 'undefined'}`);
      console.log(`        Path: ${light.abs_path}`);
      console.log(`        Color: RGB(${light.color[0].toFixed(2)}, ${light.color[1].toFixed(2)}, ${light.color[2].toFixed(2)})`);
      console.log(`        Intensity: ${light.intensity.toFixed(2)}`);

      if (light.exposure && light.exposure !== 0) {
        const effectiveIntensity = light.intensity * Math.pow(2, light.exposure);
        console.log(`        Exposure: ${light.exposure.toFixed(2)} EV (effective: ${effectiveIntensity.toFixed(2)})`);
      }

      if (light.type === 'Geometry' && light.properties) {
        console.log(`        Mesh ID: ${light.properties.geometryMeshId}`);
        console.log(`        Mesh Name: ${light.properties.meshName}`);
      }

      if (light.shadow && light.shadow.enable) {
        console.log(`        Shadows: Enabled`);
      }

      if (light.enableColorTemperature) {
        console.log(`        Color Temp: ${light.colorTemperature}K`);
      }
    }

    // Check for area light meshes (Option B representation)
    if (testConfig.expectedAreaLightMeshes !== undefined) {
      console.log(`\n  Area Light Meshes (MeshLightAPI):`);
      const numMeshes = usd.numMeshes();

      for (let i = 0; i < numMeshes; i++) {
        const mesh = usd.getMesh(i);
        if (!mesh || !mesh.isAreaLight) continue;

        // Extract light color from typed memory view
        const lightColor = mesh.lightColor ?
          [mesh.lightColor[0], mesh.lightColor[1], mesh.lightColor[2]] :
          [1.0, 1.0, 1.0];

        result.numAreaLightMeshes++;
        result.areaLightMeshes.push({
          name: mesh.primName,
          abs_path: mesh.absPath,
          lightColor: lightColor,
          lightIntensity: mesh.lightIntensity || 1.0,
          lightExposure: mesh.lightExposure || 0.0,
          lightNormalize: mesh.lightNormalize || false,
          lightMaterialSyncMode: mesh.lightMaterialSyncMode || 'materialGlowTintsLight'
        });

        console.log(`    ✓ Area Light Mesh ${result.numAreaLightMeshes}: ${mesh.primName}`);
        console.log(`        Path: ${mesh.absPath}`);
        console.log(`        Color: RGB(${lightColor[0].toFixed(2)}, ${lightColor[1].toFixed(2)}, ${lightColor[2].toFixed(2)})`);
        console.log(`        Intensity: ${(mesh.lightIntensity || 1.0).toFixed(2)}`);
        if (mesh.lightExposure && mesh.lightExposure !== 0) {
          console.log(`        Exposure: ${mesh.lightExposure.toFixed(2)} EV`);
        }
        console.log(`        Normalize: ${mesh.lightNormalize || false}`);
        console.log(`        Material Sync: ${mesh.lightMaterialSyncMode || 'materialGlowTintsLight'}`);
      }
    }

    // Verify expected counts
    let passed = true;
    if (numLights !== testConfig.expectedLights) {
      console.log(`\n✗ FAIL: Expected ${testConfig.expectedLights} lights, found ${numLights}`);
      result.error = `Light count mismatch`;
      passed = false;
    }

    if (testConfig.expectedAreaLightMeshes !== undefined &&
        result.numAreaLightMeshes !== testConfig.expectedAreaLightMeshes) {
      console.log(`\n✗ FAIL: Expected ${testConfig.expectedAreaLightMeshes} area light meshes, found ${result.numAreaLightMeshes}`);
      result.error = (result.error || '') + ` Area light mesh count mismatch`;
      passed = false;
    }

    if (passed) {
      console.log(`\n✓ PASS: Found expected ${testConfig.expectedLights} lights` +
        (testConfig.expectedAreaLightMeshes !== undefined ? ` and ${testConfig.expectedAreaLightMeshes} area light meshes` : ''));
      result.passed = true;
      results.passed++;
    } else {
      results.failed++;
    }

  } catch (err) {
    console.log(`\n✗ FAIL: ${err.message}`);
    result.error = err.message;
    results.failed++;
  }

  results.tests.push(result);
  return result;
}

async function runTests() {
  console.log('╔════════════════════════════════════════════════════════╗');
  console.log('║     UsdLux Parsing & Conversion Test Suite            ║');
  console.log('╚════════════════════════════════════════════════════════╝');
  console.log(`\nRunning ${testFiles.length} test(s)...`);

  for (const testConfig of testFiles) {
    await testFile(testConfig);
  }

  // Print summary
  console.log('\n\n' + '='.repeat(60));
  console.log('TEST SUMMARY');
  console.log('='.repeat(60));
  console.log(`Total Tests: ${results.tests.length}`);
  console.log(`Passed: ${results.passed} ✓`);
  console.log(`Failed: ${results.failed} ✗`);
  console.log('='.repeat(60));

  // Print detailed results
  console.log('\nDetailed Results:');
  for (const test of results.tests) {
    const status = test.passed ? '✓ PASS' : '✗ FAIL';
    console.log(`  ${status}: ${test.name} (${test.numLights} lights)`);
    if (test.error) {
      console.log(`    Error: ${test.error}`);
    }
  }

  // Save results to JSON
  const outputFile = 'test-results-usdlux.json';
  fs.writeFileSync(outputFile, JSON.stringify(results, null, 2), 'utf8');
  console.log(`\nResults saved to: ${outputFile}`);

  // Exit with appropriate code
  process.exit(results.failed > 0 ? 1 : 0);
}

// Run tests
runTests().catch(err => {
  console.error('Fatal error:', err.message);
  process.exit(1);
});
