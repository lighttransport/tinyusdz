#!/usr/bin/env node
/**
 * Quick test script for validating all presets
 */

import { HDRGenerator, HDRImage, Vec3 } from '../src/hdrgen.js';
import * as path from 'path';
import { fileURLToPath } from 'url';
import * as fs from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const outputDir = path.join(__dirname, '../output');

// Ensure output directory exists
if (!fs.existsSync(outputDir)) {
  fs.mkdirSync(outputDir, { recursive: true });
}

console.log('Running HDRGen tests...\n');

let passed = 0;
let failed = 0;

function test(name, fn) {
  try {
    console.log(`Testing: ${name}`);
    fn();
    console.log(`✓ ${name} passed\n`);
    passed++;
  } catch (err) {
    console.error(`✗ ${name} failed: ${err.message}\n`);
    failed++;
  }
}

// Test 1: White Furnace Generation
test('White Furnace (256x128)', () => {
  const result = HDRGenerator.generate({
    preset: 'white-furnace',
    width: 256,
    height: 128,
    projection: 'latlong',
    format: 'hdr',
    output: path.join(outputDir, 'test_furnace.hdr'),
    presetOptions: { intensity: 1.0 }
  });

  if (!result.latLongImage) throw new Error('No image generated');
  if (result.latLongImage.width !== 256) throw new Error('Wrong width');
  if (result.latLongImage.height !== 128) throw new Error('Wrong height');

  // Check first pixel is white
  const pixel = result.latLongImage.getPixel(0, 0);
  if (Math.abs(pixel.r - 1.0) > 0.01) throw new Error('Wrong intensity');
});

// Test 2: Sun & Sky Generation
test('Sun & Sky (256x128)', () => {
  const result = HDRGenerator.generate({
    preset: 'sun-sky',
    width: 256,
    height: 128,
    projection: 'latlong',
    format: 'hdr',
    output: path.join(outputDir, 'test_sunsky.hdr'),
    presetOptions: {
      sunElevation: 45,
      sunAzimuth: 135,
      sunIntensity: 100.0
    }
  });

  if (!result.latLongImage) throw new Error('No image generated');

  // Check that sky has varying intensities (not uniform)
  const p1 = result.latLongImage.getPixel(0, 0);
  const p2 = result.latLongImage.getPixel(128, 64);
  if (p1.r === p2.r && p1.g === p2.g && p1.b === p2.b) {
    throw new Error('Sky should not be uniform');
  }
});

// Test 3: Studio Lighting Generation
test('Studio Lighting (256x128)', () => {
  const result = HDRGenerator.generate({
    preset: 'studio',
    width: 256,
    height: 128,
    projection: 'latlong',
    format: 'hdr',
    output: path.join(outputDir, 'test_studio.hdr'),
    presetOptions: {}
  });

  if (!result.latLongImage) throw new Error('No image generated');
});

// Test 4: Cubemap Generation
test('Cubemap Generation (64x64 faces)', () => {
  const result = HDRGenerator.generate({
    preset: 'white-furnace',
    width: 64,
    height: 64,
    projection: 'cubemap',
    format: 'hdr',
    output: path.join(outputDir, 'test_cube'),
    presetOptions: { intensity: 1.0 }
  });

  if (!result.faces) throw new Error('No cubemap faces generated');
  if (result.faces.length !== 6) throw new Error('Should generate 6 faces');

  // Check each face
  for (const face of result.faces) {
    if (!face.image) throw new Error('Missing face image');
    if (face.image.width !== 64) throw new Error('Wrong face size');
  }
});

// Test 5: HDR Image Class
test('HDRImage class', () => {
  const img = new HDRImage(100, 50);
  if (img.width !== 100) throw new Error('Wrong width');
  if (img.height !== 50) throw new Error('Wrong height');
  if (img.data.length !== 100 * 50 * 3) throw new Error('Wrong data size');

  img.setPixel(10, 20, 1.5, 2.5, 3.5);
  const pixel = img.getPixel(10, 20);
  if (Math.abs(pixel.r - 1.5) > 0.001) throw new Error('setPixel/getPixel failed');
});

// Test 6: Vec3 math
test('Vec3 math utilities', () => {
  const v1 = new Vec3(1, 2, 3);
  const v2 = new Vec3(4, 5, 6);

  const sum = Vec3.add(v1, v2);
  if (sum.x !== 5 || sum.y !== 7 || sum.z !== 9) throw new Error('Vec3.add failed');

  const dot = Vec3.dot(v1, v2);
  if (dot !== 32) throw new Error('Vec3.dot failed');

  const len = new Vec3(3, 4, 0).length();
  if (Math.abs(len - 5) > 0.001) throw new Error('Vec3.length failed');

  const norm = new Vec3(0, 5, 0).normalize();
  if (Math.abs(norm.y - 1) > 0.001) throw new Error('Vec3.normalize failed');
});

// Test 7: Lat-Long to Direction Conversion
test('Lat-Long coordinate conversion', () => {
  // Test north pole (u=0.5, v=0)
  const north = HDRImage.latLongToDir(0.5, 0.0);
  if (Math.abs(north.y - 1) > 0.01) throw new Error('North pole conversion failed');

  // Test south pole (u=0.5, v=1)
  const south = HDRImage.latLongToDir(0.5, 1.0);
  if (Math.abs(south.y + 1) > 0.01) throw new Error('South pole conversion failed');

  // Test equator front (u=0.5, v=0.5)
  const front = HDRImage.latLongToDir(0.5, 0.5);
  if (Math.abs(front.y) > 0.01) throw new Error('Equator conversion failed');
});

// Test 8: Custom Intensity White Furnace
test('White Furnace with custom intensity', () => {
  const result = HDRGenerator.generate({
    preset: 'white-furnace',
    width: 64,
    height: 32,
    projection: 'latlong',
    format: 'hdr',
    output: path.join(outputDir, 'test_furnace_10x.hdr'),
    presetOptions: { intensity: 10.0 }
  });

  const pixel = result.latLongImage.getPixel(32, 16);
  if (Math.abs(pixel.r - 10.0) > 0.01) throw new Error('Wrong intensity');
});

// Summary
console.log('='.repeat(60));
console.log(`Test Results: ${passed} passed, ${failed} failed`);
console.log('='.repeat(60));

if (failed > 0) {
  console.error('\n✗ Some tests failed');
  process.exit(1);
} else {
  console.log('\n✓ All tests passed!');
  console.log(`\nTest outputs in: ${outputDir}/test_*.hdr`);
  process.exit(0);
}
