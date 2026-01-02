#!/usr/bin/env node
// SPDX-License-Identifier: Apache 2.0
// Test EXRDecoder with Three.js primary + TinyUSDZ fallback

import fs from 'node:fs';
import path from 'node:path';
import { decodeEXR, checkEXRSupport } from './src/tinyusdz/EXRDecoder.js';
import createTinyUSDZ from './src/tinyusdz/tinyusdz.js';

async function main() {
  console.log('=== EXR Decoder Test ===\n');

  // Initialize TinyUSDZ
  console.log('Initializing TinyUSDZ WASM module...');
  const tinyusdz = await createTinyUSDZ();
  console.log('TinyUSDZ initialized\n');

  // Test files
  const testFiles = [
    // Synthetic uncompressed (both should work)
    { path: 'test-256-gradient.exr', generate: true },
    // Real-world files with various compression
    { path: '../../models/textures/envs/forest.exr', generate: false },
    { path: '../../models/textures/envs/studio.exr', generate: false },
  ];

  // Generate synthetic test file if needed
  if (testFiles.some(f => f.generate)) {
    console.log('Generating synthetic EXR test file...');
    const { execSync } = await import('node:child_process');
    try {
      execSync('npx vite-node generate-test-exr.js 256 256 test-256-gradient.exr gradient', {
        stdio: 'pipe'
      });
      console.log('Generated: test-256-gradient.exr\n');
    } catch (err) {
      console.error('Failed to generate test file:', err.message);
    }
  }

  for (const testFile of testFiles) {
    const filePath = testFile.path;

    if (!fs.existsSync(filePath)) {
      console.log(`Skipping (not found): ${filePath}\n`);
      continue;
    }

    console.log(`Testing: ${filePath}`);
    console.log('-'.repeat(60));

    const data = fs.readFileSync(filePath);
    const buffer = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
    const fileSize = data.length;

    console.log(`File size: ${(fileSize / 1024).toFixed(1)} KB`);

    // Check support
    const support = checkEXRSupport(buffer, tinyusdz);
    console.log(`Three.js support: ${support.threejs ? 'YES' : 'NO'}`);
    console.log(`TinyUSDZ support: ${support.tinyusdz ? 'YES' : 'NO'}`);

    // Decode with automatic fallback
    const result = decodeEXR(buffer, tinyusdz, {
      outputFormat: 'float16',
      preferThreeJS: true,
      verbose: true,
    });

    if (result.success) {
      console.log(`\nDecode SUCCESS using: ${result.decoder}`);
      console.log(`  Dimensions: ${result.width} x ${result.height}`);
      console.log(`  Channels: ${result.channels}`);
      console.log(`  Format: ${result.format}`);
      console.log(`  Data size: ${(result.data.byteLength / 1024).toFixed(1)} KB`);
    } else {
      console.log(`\nDecode FAILED: ${result.error}`);
    }

    console.log('\n');
  }

  // Cleanup
  if (fs.existsSync('test-256-gradient.exr')) {
    fs.unlinkSync('test-256-gradient.exr');
  }

  console.log('=== Test Complete ===');
}

main().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
