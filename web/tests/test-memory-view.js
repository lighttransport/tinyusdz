#!/usr/bin/env node

/**
 * Test for getAssetCacheDataAsMemoryView method
 * 
 * This test verifies that the new method returns cache data as a memory view
 * and that it behaves correctly for both existing and non-existing assets.
 */

const fs = require('fs');
const path = require('path');

// Load TinyUSDZ module (prefer 64-bit/builtin 64-bit-friendly build when available)
function loadTinyUSDZModule() {
  const candidates = [
    '../js/src/tinyusdz/tinyusdz_64.js',
    '../js/src/tinyusdz/tinyusdz.js',
  ];

  for (const candidate of candidates) {
    try {
      const moduleObject = require(candidate);
      return moduleObject.default || moduleObject;
    } catch (error) {
      console.log(`⚪ Failed to load ${candidate}: ${error.message}`);
    }
  }

  throw new Error('Failed to load any TinyUSDZ JS module candidate');
}

const TinyUSDZInit = loadTinyUSDZModule();

async function runTest() {
  console.log('Loading TinyUSDZ module...');
  
  try {
    const tinyusdz = await TinyUSDZInit();
    console.log('✓ TinyUSDZ module loaded successfully');

    // Create a loader instance
    const loader = new tinyusdz.TinyUSDZLoaderNative();
    console.log('✓ Loader created');

    // Test data - simple string content
    const testAssetName = 'test-asset.txt';
    const testContent = 'Hello, World! This is test content for memory view.';
    const expectedSize = testContent.length;

    // Set the asset in cache
    console.log('Setting test asset in cache...');
    loader.setAsset(testAssetName, testContent);
    console.log(`✓ Asset '${testAssetName}' set with content: "${testContent}"`);

    // Verify the asset exists
    const hasAsset = loader.hasAsset(testAssetName);
    console.log(`✓ Asset exists check: ${hasAsset}`);
    if (!hasAsset) {
      throw new Error('Asset should exist after being set');
    }

    // Test the new getAssetCacheDataAsMemoryView method
    console.log('Testing getAssetCacheDataAsMemoryView...');
    const memoryView = loader.getAssetCacheDataAsMemoryView(testAssetName);
    
    if (memoryView === undefined) {
      throw new Error('Memory view should not be undefined for existing asset');
    }
    console.log('✓ Memory view is not undefined');

    // Check if it's a typed array
    if (!(memoryView instanceof Uint8Array)) {
      throw new Error('Memory view should be a Uint8Array');
    }
    console.log('✓ Memory view is a Uint8Array');

    // Check size
    if (memoryView.length !== expectedSize) {
      throw new Error(`Expected size ${expectedSize}, got ${memoryView.length}`);
    }
    console.log(`✓ Memory view has correct size: ${memoryView.length} bytes`);

    // Check content by converting back to string
    const decoder = new TextDecoder();
    const retrievedContent = decoder.decode(memoryView);
    if (retrievedContent !== testContent) {
      throw new Error(`Content mismatch. Expected: "${testContent}", Got: "${retrievedContent}"`);
    }
    console.log(`✓ Content matches: "${retrievedContent}"`);

    // Test with non-existing asset
    console.log('Testing with non-existing asset...');
    const nonExistingMemoryView = loader.getAssetCacheDataAsMemoryView('non-existing-asset');
    if (nonExistingMemoryView !== undefined) {
      throw new Error('Memory view should be undefined for non-existing asset');
    }
    console.log('✓ Non-existing asset returns undefined as expected');

    // Test with binary data
    console.log('Testing with binary data...');
    const binaryAssetName = 'binary-test.bin';
    const binaryData = new Uint8Array([0, 1, 2, 3, 4, 5, 6, 7]);
    const binaryString = String.fromCharCode(...binaryData);
    
    loader.setAsset(binaryAssetName, binaryString);
    const binaryMemoryView = loader.getAssetCacheDataAsMemoryView(binaryAssetName);
    
    if (binaryMemoryView.length !== binaryData.length) {
      throw new Error(`Binary data size mismatch. Expected: ${binaryData.length}, Got: ${binaryMemoryView.length}`);
    }
    
    for (let i = 0; i < binaryData.length; i++) {
      if (binaryMemoryView[i] !== binaryData[i]) {
        throw new Error(`Binary data mismatch at index ${i}. Expected: ${binaryData[i]}, Got: ${binaryMemoryView[i]}`);
      }
    }
    console.log('✓ Binary data memory view works correctly');

    // Test comparison with existing getAsset method
    console.log('Comparing with existing getAsset method...');
    const assetObj = loader.getAsset(testAssetName);
    if (!assetObj || !assetObj.data) {
      throw new Error('getAsset should return object with data');
    }
    
    // Both should have the same content
    if (assetObj.data.length !== memoryView.length) {
      throw new Error('getAsset and getAssetCacheDataAsMemoryView should return same size data');
    }
    
    for (let i = 0; i < memoryView.length; i++) {
      if (assetObj.data[i] !== memoryView[i]) {
        throw new Error(`Data mismatch at index ${i} between getAsset and getAssetCacheDataAsMemoryView`);
      }
    }
    console.log('✓ getAssetCacheDataAsMemoryView returns same data as getAsset');

    console.log('\n🎉 All tests passed!');
    console.log('✓ getAssetCacheDataAsMemoryView method works correctly');
    console.log('✓ Returns Uint8Array memory view for existing assets');
    console.log('✓ Returns undefined for non-existing assets');  
    console.log('✓ Handles both text and binary data correctly');
    console.log('✓ Consistent with existing getAsset method');

  } catch (error) {
    console.error('\n❌ Test failed:', error.message);
    console.error('Stack trace:', error.stack);
    process.exit(1);
  }
}

// Run the test
if (require.main === module) {
  console.log('='.repeat(60));
  console.log('Testing getAssetCacheDataAsMemoryView method');
  console.log('='.repeat(60));
  runTest().catch((error) => {
    console.error('\n❌ Unexpected error:', error);
    process.exit(1);
  });
}

module.exports = { runTest };
