#!/usr/bin/env node
// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment, Inc.
//
// Benchmark: TinyUSDZ vs Three.js for HDR/EXR decoding
//
// Usage:
//   npx vite-node benchmark-exr.js [options] <file.hdr|file.exr>
//   npx vite-node benchmark-exr.js --iterations 10 assets/textures/goegap_1k.hdr
//
// Options:
//   --iterations, -n <N>  Number of iterations (default: 5)
//   --warmup, -w <N>      Number of warmup iterations (default: 2)
//   --format, -f <fmt>    Output format: float32, float16 (default: float32)
//   --json                Output results as JSON
//   --help, -h            Show help

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Three.js loaders
import { EXRLoader } from 'three/examples/jsm/loaders/EXRLoader.js';
import { HDRLoader } from 'three/examples/jsm/loaders/HDRLoader.js';

// TinyUSDZ WASM module
import createTinyUSDZ from 'tinyusdz/tinyusdz.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Parse command line arguments
function parseArgs() {
  const args = process.argv.slice(2);
  const options = {
    iterations: 5,
    warmup: 2,
    format: 'float32',
    json: false,
    files: [],
    help: false,
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--iterations' || arg === '-n') {
      options.iterations = parseInt(args[++i], 10);
    } else if (arg === '--warmup' || arg === '-w') {
      options.warmup = parseInt(args[++i], 10);
    } else if (arg === '--format' || arg === '-f') {
      options.format = args[++i];
    } else if (arg === '--json') {
      options.json = true;
    } else if (arg === '--help' || arg === '-h') {
      options.help = true;
    } else if (!arg.startsWith('-')) {
      options.files.push(arg);
    }
  }

  return options;
}

function showHelp() {
  console.log(`
Benchmark: TinyUSDZ vs Three.js for HDR/EXR decoding

Usage:
  npx vite-node benchmark-exr.js [options] <file.hdr|file.exr> [...]

Options:
  --iterations, -n <N>  Number of iterations (default: 5)
  --warmup, -w <N>      Number of warmup iterations (default: 2)
  --format, -f <fmt>    Output format: float32, float16 (default: float32)
  --json                Output results as JSON
  --help, -h            Show help

Examples:
  npx vite-node benchmark-exr.js assets/textures/goegap_1k.hdr
  npx vite-node benchmark-exr.js -n 10 -f float16 test.exr test.hdr
  npx vite-node benchmark-exr.js --json assets/textures/*.hdr
`);
}

// Detect file type from extension or magic bytes
function detectFileType(filename, buffer) {
  const ext = path.extname(filename).toLowerCase();
  if (ext === '.exr') return 'exr';
  if (ext === '.hdr') return 'hdr';

  // Check magic bytes
  const view = new Uint8Array(buffer);
  // EXR magic: 0x76 0x2f 0x31 0x01 (v/1\x01)
  if (view[0] === 0x76 && view[1] === 0x2f && view[2] === 0x31 && view[3] === 0x01) {
    return 'exr';
  }
  // HDR magic: starts with "#?" (Radiance format)
  if (view[0] === 0x23 && view[1] === 0x3f) {
    return 'hdr';
  }

  return null;
}

// High-resolution timer
function hrtime() {
  return performance.now();
}

// Calculate statistics
function calcStats(times) {
  const sorted = [...times].sort((a, b) => a - b);
  const n = sorted.length;
  const sum = sorted.reduce((a, b) => a + b, 0);
  const mean = sum / n;
  const variance = sorted.reduce((acc, t) => acc + (t - mean) ** 2, 0) / n;
  const stddev = Math.sqrt(variance);
  const median = n % 2 === 0
    ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2
    : sorted[Math.floor(n / 2)];
  const min = sorted[0];
  const max = sorted[n - 1];

  return { mean, median, stddev, min, max, n };
}

// Format duration in ms
function formatDuration(ms) {
  if (ms < 1) return `${(ms * 1000).toFixed(2)} µs`;
  if (ms < 1000) return `${ms.toFixed(2)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}

// Format bytes
function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

// Benchmark TinyUSDZ decoder
async function benchmarkTinyUSDZ(tinyusdz, buffer, fileType, format, iterations, warmup) {
  const uint8Array = new Uint8Array(buffer);
  const times = [];
  let result = null;
  let outputSize = 0;

  const decodeFunc = fileType === 'exr' ? 'decodeEXR' : 'decodeHDR';

  // Check if function exists
  if (typeof tinyusdz[decodeFunc] !== 'function') {
    throw new Error(`Function ${decodeFunc} not available in TinyUSDZ module`);
  }

  // First check if decoding works
  const testResult = tinyusdz[decodeFunc](uint8Array, format);
  if (!testResult.success) {
    throw new Error(testResult.error || 'Decode failed');
  }

  // Warmup
  for (let i = 0; i < warmup; i++) {
    tinyusdz[decodeFunc](uint8Array, format);
  }

  // Benchmark
  for (let i = 0; i < iterations; i++) {
    const start = hrtime();
    result = tinyusdz[decodeFunc](uint8Array, format);
    const end = hrtime();
    times.push(end - start);
  }

  if (result && result.data) {
    outputSize = result.data.byteLength;
  }

  return {
    name: `TinyUSDZ (${decodeFunc}, ${format})`,
    times,
    stats: calcStats(times),
    result: result ? {
      width: result.width,
      height: result.height,
      channels: result.channels,
      format: result.pixelFormat || format,
      outputSize,
    } : null,
  };
}

// Benchmark Three.js decoder
function benchmarkThreeJS(buffer, fileType, iterations, warmup) {
  const times = [];
  let result = null;
  let outputSize = 0;

  const loader = fileType === 'exr' ? new EXRLoader() : new HDRLoader();

  // Warmup
  for (let i = 0; i < warmup; i++) {
    loader.parse(buffer);
  }

  // Benchmark
  for (let i = 0; i < iterations; i++) {
    const start = hrtime();
    result = loader.parse(buffer);
    const end = hrtime();
    times.push(end - start);
  }

  if (result && result.data) {
    outputSize = result.data.byteLength;
  }

  const loaderName = fileType === 'exr' ? 'EXRLoader' : 'HDRLoader';

  return {
    name: `Three.js ${loaderName}`,
    times,
    stats: calcStats(times),
    result: result ? {
      width: result.width,
      height: result.height,
      channels: result.data.length / (result.width * result.height),
      format: result.type === 1016 ? 'float16' : 'float32', // THREE.HalfFloatType = 1016
      outputSize,
    } : null,
  };
}

// Print results in table format
function printResults(filename, fileSize, results) {
  console.log('\n' + '='.repeat(80));
  console.log(`File: ${filename}`);
  console.log(`Size: ${formatBytes(fileSize)}`);
  console.log('='.repeat(80));

  // Header
  console.log('\n%-40s %12s %12s %12s %12s'.replace(/%(-?\d+)s/g, (_, n) => {
    const width = Math.abs(parseInt(n));
    return `${''.padEnd(width)}`;
  }));

  const header = ['Decoder', 'Mean', 'Median', 'Std Dev', 'Min/Max'];
  console.log(
    header[0].padEnd(40) +
    header[1].padStart(12) +
    header[2].padStart(12) +
    header[3].padStart(12) +
    header[4].padStart(16)
  );
  console.log('-'.repeat(92));

  // Results
  for (const r of results) {
    const { stats } = r;
    console.log(
      r.name.padEnd(40) +
      formatDuration(stats.mean).padStart(12) +
      formatDuration(stats.median).padStart(12) +
      formatDuration(stats.stddev).padStart(12) +
      `${formatDuration(stats.min)}/${formatDuration(stats.max)}`.padStart(16)
    );
  }

  // Output info
  console.log('\nOutput Info:');
  for (const r of results) {
    if (r.result) {
      console.log(`  ${r.name}:`);
      console.log(`    Dimensions: ${r.result.width} x ${r.result.height}`);
      console.log(`    Channels: ${r.result.channels}`);
      console.log(`    Format: ${r.result.format}`);
      console.log(`    Output Size: ${formatBytes(r.result.outputSize)}`);
    }
  }

  // Speedup comparison
  if (results.length >= 2) {
    console.log('\nSpeedup Comparison:');
    const baseline = results[results.length - 1]; // Three.js as baseline
    for (let i = 0; i < results.length - 1; i++) {
      const r = results[i];
      const speedup = baseline.stats.mean / r.stats.mean;
      const faster = speedup > 1 ? 'faster' : 'slower';
      const ratio = speedup > 1 ? speedup : 1 / speedup;
      console.log(`  ${r.name} vs ${baseline.name}: ${ratio.toFixed(2)}x ${faster}`);
    }
  }
}

// Print JSON results
function printJSONResults(allResults) {
  console.log(JSON.stringify(allResults, null, 2));
}

async function main() {
  const options = parseArgs();

  if (options.help) {
    showHelp();
    process.exit(0);
  }

  if (options.files.length === 0) {
    console.error('Error: No input files specified');
    showHelp();
    process.exit(1);
  }

  // Initialize TinyUSDZ WASM module
  if (!options.json) {
    console.log('Initializing TinyUSDZ WASM module...');
  }
  const tinyusdz = await createTinyUSDZ();
  if (!options.json) {
    console.log('TinyUSDZ WASM module initialized');
  }

  const allResults = [];

  for (const filename of options.files) {
    // Check if file exists
    if (!fs.existsSync(filename)) {
      console.error(`Error: File not found: ${filename}`);
      continue;
    }

    // Read file
    const buffer = fs.readFileSync(filename);
    const arrayBuffer = buffer.buffer.slice(
      buffer.byteOffset,
      buffer.byteOffset + buffer.byteLength
    );
    const fileSize = buffer.length;

    // Detect file type
    const fileType = detectFileType(filename, arrayBuffer);
    if (!fileType) {
      console.error(`Error: Unknown file type: ${filename}`);
      continue;
    }

    if (!options.json) {
      console.log(`\nBenchmarking: ${filename} (${fileType.toUpperCase()})`);
      console.log(`Iterations: ${options.iterations}, Warmup: ${options.warmup}`);
    }

    const results = [];

    // Benchmark TinyUSDZ with float32
    try {
      const tinyResult32 = await benchmarkTinyUSDZ(
        tinyusdz,
        arrayBuffer,
        fileType,
        'float32',
        options.iterations,
        options.warmup
      );
      results.push(tinyResult32);
    } catch (err) {
      if (!options.json) {
        console.error(`TinyUSDZ (float32) error: ${err.message}`);
      }
    }

    // Benchmark TinyUSDZ with float16
    try {
      const tinyResult16 = await benchmarkTinyUSDZ(
        tinyusdz,
        arrayBuffer,
        fileType,
        'float16',
        options.iterations,
        options.warmup
      );
      results.push(tinyResult16);
    } catch (err) {
      if (!options.json) {
        console.error(`TinyUSDZ (float16) error: ${err.message}`);
      }
    }

    // Benchmark Three.js
    try {
      const threeResult = benchmarkThreeJS(
        arrayBuffer,
        fileType,
        options.iterations,
        options.warmup
      );
      results.push(threeResult);
    } catch (err) {
      if (!options.json) {
        console.error(`Three.js error: ${err.message}`);
      }
    }

    if (options.json) {
      allResults.push({
        filename,
        fileSize,
        fileType,
        iterations: options.iterations,
        warmup: options.warmup,
        results: results.map(r => ({
          name: r.name,
          stats: r.stats,
          result: r.result,
        })),
      });
    } else {
      printResults(filename, fileSize, results);
    }
  }

  if (options.json) {
    printJSONResults(allResults);
  }

  // Summary
  if (!options.json && allResults.length === 0 && options.files.length > 0) {
    console.log('\n' + '='.repeat(80));
    console.log('Benchmark Complete');
    console.log('='.repeat(80));
  }
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
