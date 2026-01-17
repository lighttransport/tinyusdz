#!/usr/bin/env node
/**
 * Generate all preset examples
 */

import { HDRGenerator } from '../src/hdrgen.js';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const outputDir = path.join(__dirname, '../output');

console.log('Generating all preset examples...\n');

// 1. White Furnace (for testing)
console.log('--- White Furnace ---');
HDRGenerator.generate({
  preset: 'white-furnace',
  width: 1024,
  height: 512,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'white_furnace_1k.hdr'),
  presetOptions: { intensity: 1.0 }
});

// 2. Sun & Sky (Default - afternoon)
console.log('\n--- Sun & Sky (Afternoon) ---');
HDRGenerator.generate({
  preset: 'sun-sky',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'sunsky_afternoon_2k.hdr'),
  presetOptions: {
    sunElevation: 45,
    sunAzimuth: 135,
    sunIntensity: 100.0,
    skyIntensity: 0.5
  }
});

// 3. Sun & Sky (Sunset)
console.log('\n--- Sun & Sky (Sunset) ---');
HDRGenerator.generate({
  preset: 'sun-sky',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'sunsky_sunset_2k.hdr'),
  presetOptions: {
    sunElevation: 5,
    sunAzimuth: 270,
    sunIntensity: 150.0,
    skyIntensity: 0.3
  }
});

// 4. Sun & Sky (Noon)
console.log('\n--- Sun & Sky (Noon) ---');
HDRGenerator.generate({
  preset: 'sun-sky',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'sunsky_noon_2k.hdr'),
  presetOptions: {
    sunElevation: 85,
    sunAzimuth: 0,
    sunIntensity: 200.0,
    skyIntensity: 0.8
  }
});

// 5. Studio Lighting (Default)
console.log('\n--- Studio Lighting (Default) ---');
HDRGenerator.generate({
  preset: 'studio',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'studio_default_2k.hdr'),
  presetOptions: {}
});

// 6. Studio Lighting (High Key)
console.log('\n--- Studio Lighting (High Key) ---');
HDRGenerator.generate({
  preset: 'studio',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'studio_highkey_2k.hdr'),
  presetOptions: {
    keyIntensity: 80.0,
    fillIntensity: 30.0,
    rimIntensity: 10.0,
    ambientIntensity: 1.0
  }
});

// 7. Studio Lighting (Low Key)
console.log('\n--- Studio Lighting (Low Key) ---');
HDRGenerator.generate({
  preset: 'studio',
  width: 2048,
  height: 1024,
  projection: 'latlong',
  format: 'hdr',
  output: path.join(outputDir, 'studio_lowkey_2k.hdr'),
  presetOptions: {
    keyIntensity: 30.0,
    fillIntensity: 5.0,
    rimIntensity: 40.0,
    ambientIntensity: 0.1
  }
});

// 8. Cubemap Example (Studio)
console.log('\n--- Cubemap (Studio) ---');
HDRGenerator.generate({
  preset: 'studio',
  width: 512,
  height: 512,
  projection: 'cubemap',
  format: 'hdr',
  output: path.join(outputDir, 'studio_cube'),
  presetOptions: {}
});

console.log('\n=== All examples generated successfully! ===');
console.log(`Output directory: ${outputDir}\n`);
