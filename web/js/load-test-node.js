// require nodejs v24.0 or later(to load wasm)
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';
import { TinyUSDZLoaderUtils } from 'tinyusdz/TinyUSDZLoaderUtils.js';
import { TinyUSDZComposer } from 'tinyusdz/TinyUSDZComposer.js';

import fs from 'node:fs';
import { pipeline } from 'node:stream/promises';

function reportMemUsage() {
 const used = process.memoryUsage()
 const messages = []
 for (let key in used) {
    console.log(`${key}: ${Math.round(used[key] / 1024 / 1024 * 100) / 100} MB`)
  }
}

function loadFile(filename) {
  try {
    const data = fs.readFileSync(filename);
    const mimeType = 'application/octet-stream';
    const blob = new Blob([data], { type: mimeType });

    const f = new File([blob], filename, { type: blob.type });

    return f;
    //const base64 = data.toString('base64');
    //data = null;
    //return `data:${mimeType};base64,${base64}`;
    //console.log(data);
  } catch (err) {
    console.error(err);
  }
  return null;
}

// Stream file reading and set to Asset using streaming API
async function streamFileToAsset(filename, loader, assetName) {
  return new Promise((resolve, reject) => {
    const stats = fs.statSync(filename);
    const fileSize = stats.size;
    const chunkSize = 65536; // 64KB chunks
    
    console.log(`Starting streaming asset: ${assetName}, size: ${fileSize} bytes`);
    
    // Start the streaming asset
    if (!loader.native_ || !loader.native_.TinyUSDZLoaderNative) {
      reject(new Error('Native module not initialized'));
      return;
    }
    
    const usd = new loader.native_.TinyUSDZLoaderNative();
    
    // Initialize streaming for this asset
    if (!usd.startStreamingAsset(assetName, fileSize)) {
      reject(new Error('Failed to start streaming asset'));
      return;
    }
    
    const stream = fs.createReadStream(filename, {
      highWaterMark: chunkSize
    });
    
    let bytesRead = 0;
    
    stream.on('data', (chunk) => {
      // Convert chunk to Uint8Array then to string for the API
      const uint8Array = new Uint8Array(chunk);
      
      // Append the chunk to the streaming asset
      if (!usd.appendAssetChunk(assetName, uint8Array)) {
        stream.destroy();
        reject(new Error('Failed to append asset chunk'));
        return;
      }
      
      bytesRead += chunk.length;
      
      // Check progress
      const progress = usd.getStreamingProgress(assetName);
      if (progress && progress.exists) {
        // Use fileSize if expected is not provided
        const expectedBytes = progress.expected || fileSize;
        const percentage = expectedBytes > 0 ? Math.round(progress.current/expectedBytes * 100) : 0;
        console.log(`Progress: ${progress.current}/${expectedBytes} bytes (${percentage}%)`);
      }
    });
    
    stream.on('end', () => {
      console.log(`Stream ended, finalizing asset: ${assetName}`);
      
      // Finalize the streaming asset
      if (!usd.finalizeStreamingAsset(assetName)) {
        reject(new Error('Failed to finalize streaming asset'));
        return;
      }
      
      console.log(`Asset ${assetName} successfully loaded via streaming`);
      resolve(usd);
    });
    
    stream.on('error', (err) => {
      console.error('Stream error:', err);
      reject(err);
    });
  });
}

console.log(process.versions.node);

function checkMemory64Support() {
    try {
        // Try creating a 64-bit memory
        const memory = new WebAssembly.Memory({
            initial: 1,
            maximum: 65536,
            index: 'i64'  // This specifies 64-bit indexing
        });
        return true;
    } catch (e) {
        return false;
    }
}

console.log("memory64:", checkMemory64Support());

// Parse command line arguments
const args = process.argv.slice(2);
if (args.length === 0) {
  console.log('Usage: node load-test-node.js <path-to-usd-file>');
  console.log('');
  console.log('Examples:');
  console.log('  node load-test-node.js model.usdz');
  console.log('  node load-test-node.js ../../models/suzanne-subd-lv4.usdc');
  process.exit(1);
}

const usd_filename = args[0];

// Check if file exists
if (!fs.existsSync(usd_filename)) {
  console.error(`Error: File not found: ${usd_filename}`);
  process.exit(1);
}

async function initScene() {

  const loader = new TinyUSDZLoader();
  const memory64 = false; //checkMemory64Support();
  await loader.init({useMemory64: false});
  loader.setMaxMemoryLimitMB(200);

  // Option 1: Use traditional file loading (existing method)
  console.log("\n=== Traditional file loading ===");
  console.log(`File: ${usd_filename}`);
  const f = loadFile(usd_filename);
  if (!f) {
    console.error('Failed to load file');
    process.exit(1);
  }
  const url = URL.createObjectURL(f);
  //const usd = await loader.loadTestAsync(url);
  const usd = await loader.loadAsync(url);
  //console.log("Traditional loading completed");
  console.log("Load completed");
  reportMemUsage();

  /*
  // Option 2: Use streaming API to load asset
  console.log("\n=== Streaming file loading ===");
  try {
    // Stream the file content to the asset cache
    const assetName = "streamed_model.usdc";
    const streamedUsd = await streamFileToAsset(usd_filename, loader, assetName);
    
    // Now the asset is in the cache and can be used
    if (streamedUsd.hasAsset(assetName)) {
      console.log(`Asset ${assetName} is now available in cache`);
      
      // Get UUID of the streamed asset
      const uuid = streamedUsd.getAssetUUID(assetName);
      console.log(`Asset UUID: ${uuid}`);
      
      // You can now load from the cached asset
      // The asset is already in memory and can be accessed via the resolver
      // Skip loadTest for now - just verify streaming worked
      // const testResult = streamedUsd.loadTest(assetName, assetData);
      // console.log(`Load test result: ${testResult}`);
    }
    
    console.log("Streaming loading completed");
    reportMemUsage();
  } catch (err) {
    console.error("Streaming failed:", err);
  }
  */

}

console.log(WebAssembly);
/*
Object [WebAssembly] {
  compile: [Function: compile],
  validate: [Function: validate],
  instantiate: [Function: instantiate]
}
*/


initScene().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
