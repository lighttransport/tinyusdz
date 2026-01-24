// test-stream-load.js
// Simple examples demonstrating streaming transfer from JS to WASM
//
// Usage (Node.js):
//   node test-stream-load.js <path-to-usd-file>
//
// Usage (Browser):
//   Include this script and call the browser examples with a URL

import { TinyUSDZLoader } from './src/tinyusdz/TinyUSDZLoader.js';

// ============================================================
// Memory Usage Reporting (Node.js only)
// ============================================================
function reportMemUsage() {
  const used = process.memoryUsage();
  for (let key in used) {
    console.log(`${key}: ${Math.round(used[key] / 1024 / 1024 * 100) / 100} MB`);
  }
}

// ============================================================
// Example 1: Stream fetch from URL (Browser/Node.js with fetch)
// ============================================================
async function exampleStreamFetch(url) {
  console.log('=== Stream Fetch Example ===');
  console.log(`URL: ${url}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const startTime = performance.now();

  try {
    // Stream fetch directly to WASM with progress reporting
    const result = await loader.streamFetchToWasm(url, 'test-asset.usd', {
      onProgress: (loaded, total) => {
        const percent = total > 0 ? ((loaded / total) * 100).toFixed(1) : '?';
        process.stdout.write(`\rStreaming: ${(loaded / 1024).toFixed(1)} KB / ${(total / 1024).toFixed(1)} KB (${percent}%)`);
      }
    });

    console.log('\n');
    console.log('Stream result:', { success: result.success, bytesTransferred: result.bytesTransferred, assetPath: result.assetPath });

    const transferTime = performance.now() - startTime;
    console.log(`Transfer time: ${transferTime.toFixed(2)} ms`);

    // Load from cached asset using the same instance (cache is per-instance)
    const usd = result.usdInstance;
    const loadOk = usd.loadFromCachedAsset('test-asset.usd');

    if (loadOk) {
      console.log('USD loaded successfully from cached asset!');
      console.log(`Total time: ${(performance.now() - startTime).toFixed(2)} ms`);
      reportMemUsage();
    } else {
      console.error('Failed to load USD:', usd.error());
    }

    return result;
  } catch (error) {
    console.error('Stream fetch failed:', error);
    throw error;
  }
}

// ============================================================
// Example 2: Stream file read (Node.js only)
// ============================================================
async function exampleStreamFile(filePath) {
  console.log('=== Stream File Example (Node.js) ===');
  console.log(`File: ${filePath}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const startTime = performance.now();

  try {
    // Stream file directly to WASM with progress reporting
    const result = await loader.streamFileToWasm(filePath, 'test-file.usd', {
      chunkSize: 64 * 1024, // 64KB chunks
      onProgress: (loaded, total) => {
        const percent = ((loaded / total) * 100).toFixed(1);
        process.stdout.write(`\rStreaming: ${(loaded / 1024).toFixed(1)} KB / ${(total / 1024).toFixed(1)} KB (${percent}%)`);
      }
    });

    console.log('\n');
    console.log('Stream result:', { success: result.success, bytesTransferred: result.bytesTransferred, assetPath: result.assetPath });

    const transferTime = performance.now() - startTime;
    console.log(`Transfer time: ${transferTime.toFixed(2)} ms`);

    // Load from cached asset using the same instance (cache is per-instance)
    const usd = result.usdInstance;
    const loadOk = usd.loadFromCachedAsset('test-file.usd');

    if (loadOk) {
      console.log('USD loaded successfully from cached asset!');
      console.log(`Total time: ${(performance.now() - startTime).toFixed(2)} ms`);
      reportMemUsage();
    } else {
      console.error('Failed to load USD:', usd.error());
    }

    return result;
  } catch (error) {
    console.error('Stream file failed:', error);
    throw error;
  }
}

// ============================================================
// Example 3: High-level loadWithStreaming (combines fetch + parse)
// ============================================================
async function exampleLoadWithStreaming(url) {
  console.log('=== Load With Streaming Example ===');
  console.log(`URL: ${url}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const startTime = performance.now();

  try {
    const usd = await loader.loadWithStreaming(url, {
      onProgress: (info) => {
        const bar = '='.repeat(Math.floor(info.progress * 20)).padEnd(20, ' ');
        process.stdout.write(`\r[${bar}] ${info.percentage.toFixed(0)}% - ${info.message}`);
      }
    });

    console.log('\n');
    console.log(`Total time: ${(performance.now() - startTime).toFixed(2)} ms`);
    console.log('USD object loaded:', usd ? 'success' : 'failed');
    reportMemUsage();

    return usd;
  } catch (error) {
    console.error('\nLoad with streaming failed:', error);
    throw error;
  }
}

// ============================================================
// Example 4: Stream multiple assets in parallel
// ============================================================
async function exampleStreamMultiple(assets) {
  console.log('=== Stream Multiple Assets Example ===');
  console.log(`Assets: ${assets.length}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const startTime = performance.now();

  try {
    const results = await loader.streamFetchMultipleToWasm(assets, {
      concurrency: 4,
      onProgress: (completed, total, currentAsset) => {
        console.log(`Progress: ${completed}/${total} - Completed: ${currentAsset}`);
      },
      onAssetProgress: (assetPath, loaded, total) => {
        // Per-asset progress (optional)
      }
    });

    console.log('\nResults:');
    for (const result of results) {
      if (result.success) {
        console.log(`  OK: ${result.assetPath} (${result.bytesTransferred} bytes)`);
      } else {
        console.log(`  FAIL: ${result.assetPath} - ${result.error}`);
      }
    }

    console.log(`Total time: ${(performance.now() - startTime).toFixed(2)} ms`);
    reportMemUsage();

    return results;
  } catch (error) {
    console.error('Stream multiple failed:', error);
    throw error;
  }
}

// ============================================================
// Example 5: USD Load Only (no scene conversion for Three.js)
// ============================================================
// This mode loads USD as a Layer only, without converting to RenderScene.
// Useful for measuring pure USD parsing memory usage.
async function exampleLoadOnly(filePath) {
  console.log('=== USD Load Only (No Scene Conversion) ===');
  console.log(`File: ${filePath}`);
  console.log('Mode: Layer-only parsing (no RenderScene conversion)');

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const fs = await import('fs');
  const fileData = fs.readFileSync(filePath);
  const fileSizeMB = (fileData.length / (1024 * 1024)).toFixed(2);
  console.log(`File size: ${fileSizeMB} MB`);

  const startTime = performance.now();

  try {
    const usd = new loader.native_.TinyUSDZLoaderNative();

    // Load as Layer only - no RenderScene conversion
    const loadOk = usd.loadAsLayerFromBinary(fileData, filePath);
    const loadTime = performance.now() - startTime;

    if (loadOk) {
      console.log(`\nUSD Layer loaded successfully!`);
      console.log(`Load time: ${loadTime.toFixed(2)} ms`);

      // Try to get memory stats if available
      try {
        const stats = usd.getMemoryStats();
        console.log('\nWASM Memory Stats:');
        console.log(`  Meshes: ${stats.numMeshes} (should be 0 for layer-only)`);
        console.log(`  Materials: ${stats.numMaterials}`);
        console.log(`  Buffer Memory: ${stats.bufferMemoryMB?.toFixed(2) || 0} MB`);
      } catch (e) {
        // getMemoryStats may not exist
      }
    } else {
      console.error('Failed to load USD:', usd.error());
    }

    console.log('\nNode.js Memory Usage:');
    reportMemUsage();

    return { success: loadOk, loadTime };
  } catch (error) {
    console.error('Load only failed:', error);
    throw error;
  }
}

// ============================================================
// Example 6: Compare full load vs load-only
// ============================================================
async function exampleCompareLoadModes(filePath) {
  console.log('=== Compare Load Modes ===');
  console.log(`File: ${filePath}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const fs = await import('fs');
  const fileData = fs.readFileSync(filePath);
  const fileSizeMB = (fileData.length / (1024 * 1024)).toFixed(2);
  console.log(`File size: ${fileSizeMB} MB`);

  // Mode 1: Load Only (Layer parsing, no scene conversion)
  console.log('\n--- Mode 1: Load Only (Layer) ---');
  const loadOnlyStart = performance.now();

  const usd1 = new loader.native_.TinyUSDZLoaderNative();
  const ok1 = usd1.loadAsLayerFromBinary(fileData, filePath);
  const loadOnlyTime = performance.now() - loadOnlyStart;

  console.log(`  Load time: ${loadOnlyTime.toFixed(2)} ms`);
  console.log(`  Load result: ${ok1 ? 'success' : 'failed'}`);
  console.log('  Memory (Layer only):');
  reportMemUsage();

  // Reset for fair comparison
  usd1.reset();

  // Force GC if available
  if (global.gc) {
    global.gc();
    console.log('  (GC triggered)');
  }

  // Mode 2: Full Load (with RenderScene conversion)
  console.log('\n--- Mode 2: Full Load (with Scene Conversion) ---');
  const fullLoadStart = performance.now();

  const usd2 = new loader.native_.TinyUSDZLoaderNative();
  const ok2 = usd2.loadFromBinary(fileData, filePath);
  const fullLoadTime = performance.now() - fullLoadStart;

  console.log(`  Load time: ${fullLoadTime.toFixed(2)} ms`);
  console.log(`  Load result: ${ok2 ? 'success' : 'failed'}`);

  // Get render scene stats
  try {
    const stats = usd2.getMemoryStats();
    console.log('  RenderScene Stats:');
    console.log(`    Meshes: ${stats.numMeshes}`);
    console.log(`    Materials: ${stats.numMaterials}`);
    console.log(`    Textures: ${stats.numTextures}`);
    console.log(`    Images: ${stats.numImages}`);
    console.log(`    Buffers: ${stats.numBuffers}`);
    console.log(`    Buffer Memory: ${stats.bufferMemoryMB?.toFixed(2) || 0} MB`);
  } catch (e) {
    // getMemoryStats may not exist
  }

  console.log('  Memory (Full load):');
  reportMemUsage();

  // Summary
  console.log('\n--- Summary ---');
  console.log(`  Load Only (Layer):     ${loadOnlyTime.toFixed(2)} ms`);
  console.log(`  Full Load (RenderScene): ${fullLoadTime.toFixed(2)} ms`);
  console.log(`  Scene conversion overhead: ${(fullLoadTime - loadOnlyTime).toFixed(2)} ms`);
  if (fullLoadTime > 0) {
    console.log(`  Overhead ratio: ${((fullLoadTime / loadOnlyTime - 1) * 100).toFixed(1)}% slower`);
  }
}

// ============================================================
// Example 7: Compare streaming vs traditional loading
// ============================================================
async function exampleComparePerformance(filePath) {
  console.log('=== Performance Comparison ===');
  console.log(`File: ${filePath}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  const fs = await import('fs');

  // Traditional loading (read entire file into JS, then copy to WASM)
  console.log('\n--- Traditional Loading ---');
  const traditionalStart = performance.now();

  let fileData = fs.readFileSync(filePath);
  const fileSizeMB = (fileData.length / (1024 * 1024)).toFixed(2);
  console.log(`  File size: ${fileSizeMB} MB`);
  const traditionalReadTime = performance.now() - traditionalStart;

  const usd1 = new loader.native_.TinyUSDZLoaderNative();
  const ok1 = usd1.loadFromBinary(fileData, filePath);
  const traditionalTotalTime = performance.now() - traditionalStart;

  console.log(`  Read time: ${traditionalReadTime.toFixed(2)} ms`);
  console.log(`  Total time: ${traditionalTotalTime.toFixed(2)} ms`);
  console.log(`  Load result: ${ok1 ? 'success' : 'failed'}`);
  console.log('  Memory after traditional load:');
  reportMemUsage();

  // Clear memory before streaming test for fair comparison
  console.log('\n--- Clearing Memory ---');

  // Reset WASM memory (clear render scene, assets, caches)
  try {
    usd1.reset();
    console.log('  WASM memory reset');
  } catch (e) {
    // reset() may not exist in older builds
    try {
      usd1.clearAssets();
      console.log('  WASM assets cleared (fallback)');
    } catch (e2) {
      console.log('  Could not clear WASM memory');
    }
  }

  // Clear JS file data reference
  fileData = null;

  // Try to trigger garbage collection if available
  // Run with: node --expose-gc test-stream-load.js file.usd --compare
  if (global.gc) {
    global.gc();
    console.log('  GC triggered');
  } else {
    console.log('  GC not available (run with --expose-gc for manual GC)');
  }

  // Small delay to allow GC to run
  await new Promise(resolve => setTimeout(resolve, 100));

  console.log('  Memory after cleanup:');
  reportMemUsage();

  // Streaming loading (chunks transferred directly to WASM)
  console.log('\n--- Streaming Loading ---');
  const streamingStart = performance.now();

  const streamResult = await loader.streamFileToWasm(filePath, 'streaming-test.usd', {
    chunkSize: 64 * 1024
  });
  const streamingTransferTime = performance.now() - streamingStart;

  // Use the same instance to access the cached asset
  const usd2 = streamResult.usdInstance;
  const ok2 = usd2.loadFromCachedAsset('streaming-test.usd');
  const streamingTotalTime = performance.now() - streamingStart;

  console.log(`  Transfer time: ${streamingTransferTime.toFixed(2)} ms`);
  console.log(`  Total time: ${streamingTotalTime.toFixed(2)} ms`);
  console.log(`  Load result: ${ok2 ? 'success' : 'failed'}`);
  console.log('  Memory after streaming load:');
  reportMemUsage();

  // Summary
  console.log('\n--- Summary ---');
  console.log(`  Traditional: ${traditionalTotalTime.toFixed(2)} ms`);
  console.log(`  Streaming: ${streamingTotalTime.toFixed(2)} ms`);
  console.log(`  Memory benefit: Streaming frees JS memory chunk-by-chunk`);
  console.log('  Note: Run with "node --expose-gc" for accurate memory comparison');
}

// ============================================================
// CLI Entry Point
// ============================================================
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    console.log('Usage: node test-stream-load.js <path-to-usd-file> [options]');
    console.log('');
    console.log('Options:');
    console.log('  --load-only     Load USD only (no scene conversion) - measure pure parsing');
    console.log('  --compare-modes Compare load-only vs full load with scene conversion');
    console.log('  --compare       Compare streaming vs traditional loading');
    console.log('  --url           Treat input as URL (auto-detected for http/https)');
    console.log('');
    console.log('Examples:');
    console.log('  node test-stream-load.js model.usdz');
    console.log('  node test-stream-load.js model.usdz --load-only');
    console.log('  node test-stream-load.js model.usdz --compare-modes');
    console.log('  node test-stream-load.js model.usdz --compare');
    console.log('  node test-stream-load.js https://example.com/model.usdz --url');
    process.exit(1);
  }

  const input = args[0];
  const isUrl = args.includes('--url') || input.startsWith('http://') || input.startsWith('https://');
  const doCompare = args.includes('--compare');
  const doLoadOnly = args.includes('--load-only');
  const doCompareModes = args.includes('--compare-modes');

  try {
    if (isUrl) {
      // URL-based examples
      await exampleStreamFetch(input);
      console.log('\n');
      await exampleLoadWithStreaming(input);
    } else {
      // File-based examples
      if (doLoadOnly) {
        await exampleLoadOnly(input);
      } else if (doCompareModes) {
        await exampleCompareLoadModes(input);
      } else if (doCompare) {
        await exampleComparePerformance(input);
      } else {
        await exampleStreamFile(input);
      }
    }

    console.log('\nAll examples completed successfully!');
  } catch (error) {
    console.error('\nExample failed:', error);
    process.exit(1);
  }
}

// Run if executed directly
const isMainModule = import.meta.url === `file://${process.argv[1]}`;
if (isMainModule) {
  main();
}

// Export for use as module
export {
  exampleStreamFetch,
  exampleStreamFile,
  exampleLoadWithStreaming,
  exampleStreamMultiple,
  exampleLoadOnly,
  exampleCompareLoadModes,
  exampleComparePerformance
};
