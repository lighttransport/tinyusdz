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

    return results;
  } catch (error) {
    console.error('Stream multiple failed:', error);
    throw error;
  }
}

// ============================================================
// Example 5: Compare streaming vs traditional loading
// ============================================================
async function exampleComparePerformance(filePath) {
  console.log('=== Performance Comparison ===');
  console.log(`File: ${filePath}`);

  const loader = new TinyUSDZLoader();
  await loader.init({ useMemory64: true });

  // Traditional loading (read entire file into JS, then copy to WASM)
  console.log('\n--- Traditional Loading ---');
  const traditionalStart = performance.now();

  const fs = await import('fs');
  const fileData = fs.readFileSync(filePath);
  const traditionalReadTime = performance.now() - traditionalStart;

  const usd1 = new loader.native_.TinyUSDZLoaderNative();
  const ok1 = usd1.loadFromBinary(fileData, filePath);
  const traditionalTotalTime = performance.now() - traditionalStart;

  console.log(`  Read time: ${traditionalReadTime.toFixed(2)} ms`);
  console.log(`  Total time: ${traditionalTotalTime.toFixed(2)} ms`);
  console.log(`  Load result: ${ok1 ? 'success' : 'failed'}`);

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

  // Summary
  console.log('\n--- Summary ---');
  console.log(`  Traditional: ${traditionalTotalTime.toFixed(2)} ms`);
  console.log(`  Streaming: ${streamingTotalTime.toFixed(2)} ms`);
  console.log(`  Memory benefit: Streaming frees JS memory chunk-by-chunk`);
}

// ============================================================
// CLI Entry Point
// ============================================================
async function main() {
  const args = process.argv.slice(2);

  if (args.length === 0) {
    console.log('Usage: node test-stream-load.js <path-to-usd-file> [--compare]');
    console.log('');
    console.log('Examples:');
    console.log('  node test-stream-load.js model.usdz');
    console.log('  node test-stream-load.js model.usdz --compare');
    console.log('  node test-stream-load.js https://example.com/model.usdz --url');
    process.exit(1);
  }

  const input = args[0];
  const isUrl = args.includes('--url') || input.startsWith('http://') || input.startsWith('https://');
  const doCompare = args.includes('--compare');

  try {
    if (isUrl) {
      // URL-based examples
      await exampleStreamFetch(input);
      console.log('\n');
      await exampleLoadWithStreaming(input);
    } else {
      // File-based examples
      if (doCompare) {
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
  exampleComparePerformance
};
