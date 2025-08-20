/**
 * Zero-Copy Utility Functions for TinyUSDZ WebAssembly
 * 
 * This module provides helper functions to easily use the zero-copy
 * setAssetFromRawPointer functionality with Uint8Arrays.
 */

/**
 * Get the raw pointer address for a Uint8Array in the Emscripten heap
 * @param {Object} Module - The Emscripten module instance
 * @param {Uint8Array} uint8Array - The data to get pointer for
 * @returns {number} Pointer address in the heap
 */
function getPointerFromUint8Array(Module, uint8Array) {
  if (!(uint8Array instanceof Uint8Array)) {
    throw new Error('Input must be a Uint8Array');
  }
  
  // Get the data pointer from the heap
  // This assumes the Uint8Array is backed by the same heap as Module.HEAPU8
  const dataPtr = Module.HEAPU8.subarray(
    uint8Array.byteOffset,
    uint8Array.byteOffset + uint8Array.byteLength
  ).byteOffset;
  
  return dataPtr;
}

/**
 * High-level helper to set an asset using zero-copy method
 * @param {Object} Module - The Emscripten module instance
 * @param {Object} loader - TinyUSDZLoaderNative instance
 * @param {string} assetName - Name of the asset
 * @param {Uint8Array} uint8Array - Binary data
 * @returns {boolean} True if asset was overwritten, false if newly created
 */
function setAssetZeroCopy(Module, loader, assetName, uint8Array) {
  try {
    const dataPtr = getPointerFromUint8Array(Module, uint8Array);
    return loader.setAssetFromRawPointer(assetName, dataPtr, uint8Array.length);
  } catch (error) {
    console.warn('Zero-copy method failed, falling back to traditional method:', error.message);
    // Fallback to traditional method
    const binaryString = String.fromCharCode(...uint8Array);
    loader.setAsset(assetName, binaryString);
    return loader.hasAsset(assetName);
  }
}

/**
 * Load a file and set it as an asset using zero-copy method
 * @param {Object} Module - The Emscripten module instance  
 * @param {Object} loader - TinyUSDZLoaderNative instance
 * @param {string} assetName - Name of the asset
 * @param {string} filePath - Path to file (for Node.js) or URL (for browser)
 * @returns {Promise<boolean>} Promise that resolves to success status
 */
async function loadFileAsAssetZeroCopy(Module, loader, assetName, filePath) {
  let arrayBuffer;
  
  if (typeof window !== 'undefined') {
    // Browser environment
    const response = await fetch(filePath);
    if (!response.ok) {
      throw new Error(`Failed to fetch ${filePath}: ${response.statusText}`);
    }
    arrayBuffer = await response.arrayBuffer();
  } else {
    // Node.js environment
    const fs = require('fs').promises;
    const buffer = await fs.readFile(filePath);
    arrayBuffer = buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
  }
  
  const uint8Array = new Uint8Array(arrayBuffer);
  return setAssetZeroCopy(Module, loader, assetName, uint8Array);
}

/**
 * Performance comparison between traditional and zero-copy methods
 * @param {Object} Module - The Emscripten module instance
 * @param {Object} loader - TinyUSDZLoaderNative instance
 * @param {Uint8Array} testData - Data to use for comparison
 * @returns {Object} Performance comparison results
 */
function comparePerformance(Module, loader, testData) {
  const results = {
    dataSize: testData.length,
    traditional: {},
    zeroCopy: {}
  };
  
  // Traditional method
  console.time('traditional');
  const binaryString = String.fromCharCode(...testData);
  loader.setAsset('perf-test-traditional', binaryString);
  console.timeEnd('traditional');
  
  // Zero-copy method
  console.time('zeroCopy');
  const dataPtr = getPointerFromUint8Array(Module, testData);
  loader.setAssetFromRawPointer('perf-test-zerocopy', dataPtr, testData.length);
  console.timeEnd('zeroCopy');
  
  // Verify both methods worked
  results.traditional.success = loader.hasAsset('perf-test-traditional');
  results.zeroCopy.success = loader.hasAsset('perf-test-zerocopy');
  
  return results;
}

/**
 * Validate that a Uint8Array can be used with zero-copy method
 * @param {Object} Module - The Emscripten module instance
 * @param {Uint8Array} uint8Array - Array to validate
 * @returns {Object} Validation results
 */
function validateUint8Array(Module, uint8Array) {
  const validation = {
    isUint8Array: uint8Array instanceof Uint8Array,
    hasBuffer: uint8Array.buffer instanceof ArrayBuffer,
    size: uint8Array.length,
    byteOffset: uint8Array.byteOffset,
    byteLength: uint8Array.byteLength,
    isCompatible: false,
    warnings: []
  };
  
  if (!validation.isUint8Array) {
    validation.warnings.push('Input is not a Uint8Array');
  }
  
  if (validation.size === 0) {
    validation.warnings.push('Array is empty');
  }
  
  if (validation.size > 1024 * 1024 * 100) { // 100MB
    validation.warnings.push('Array is very large (>100MB), consider streaming');
  }
  
  // Check if the array is backed by the same heap as Module.HEAPU8
  try {
    getPointerFromUint8Array(Module, uint8Array);
    validation.isCompatible = true;
  } catch (error) {
    validation.warnings.push(`Not compatible with zero-copy: ${error.message}`);
  }
  
  return validation;
}

// Export for both Node.js and browser environments
if (typeof module !== 'undefined' && module.exports) {
  // Node.js
  module.exports = {
    getPointerFromUint8Array,
    setAssetZeroCopy,
    loadFileAsAssetZeroCopy,
    comparePerformance,
    validateUint8Array
  };
} else {
  // Browser
  window.TinyUSDZZeroCopyUtils = {
    getPointerFromUint8Array,
    setAssetZeroCopy,
    loadFileAsAssetZeroCopy,
    comparePerformance,
    validateUint8Array
  };
}