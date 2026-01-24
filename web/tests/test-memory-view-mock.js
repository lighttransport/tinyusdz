#!/usr/bin/env node

/**
 * Mock test for getAssetCacheDataAsMemoryView method
 * 
 * This test demonstrates the expected behavior of the new method
 * without requiring a fully built WebAssembly module.
 */

console.log('='.repeat(60));
console.log('Mock Test for getAssetCacheDataAsMemoryView method');
console.log('='.repeat(60));

// Mock implementation that simulates the expected behavior
class MockEMAssetResolver {
  constructor() {
    this.cache = new Map();
  }

  add(assetName, binaryData) {
    this.cache.set(assetName, {
      binary: binaryData,
      sha256_hash: 'mock-hash-' + assetName
    });
    return this.cache.has(assetName);
  }

  has(assetName) {
    return this.cache.has(assetName);
  }

  getCacheDataAsMemoryView(assetName) {
    if (!this.cache.has(assetName)) {
      return undefined;
    }
    
    const entry = this.cache.get(assetName);
    // Simulate converting string to Uint8Array (as would happen in WebAssembly)
    const encoder = new TextEncoder();
    return encoder.encode(entry.binary);
  }
}

// Mock TinyUSDZLoaderNative
class MockTinyUSDZLoaderNative {
  constructor() {
    this.em_resolver_ = new MockEMAssetResolver();
  }

  setAsset(name, binary) {
    this.em_resolver_.add(name, binary);
  }

  hasAsset(name) {
    return this.em_resolver_.has(name);
  }

  getAssetCacheDataAsMemoryView(name) {
    return this.em_resolver_.getCacheDataAsMemoryView(name);
  }

  getAsset(name) {
    if (!this.em_resolver_.has(name)) {
      return undefined;
    }
    const memView = this.getAssetCacheDataAsMemoryView(name);
    return {
      name: name,
      data: memView,
      sha256: 'mock-hash-' + name
    };
  }
}

function runMockTest() {
  console.log('Running mock test...\n');
  
  try {
    // Create mock loader
    const loader = new MockTinyUSDZLoaderNative();
    console.log('✓ Mock loader created');

    // Test data
    const testAssetName = 'test-asset.txt';
    const testContent = 'Hello, World! This is test content for memory view.';
    const expectedSize = new TextEncoder().encode(testContent).length;

    // Set the asset in cache
    loader.setAsset(testAssetName, testContent);
    console.log(`✓ Asset '${testAssetName}' set with content: "${testContent}"`);

    // Verify the asset exists
    const hasAsset = loader.hasAsset(testAssetName);
    if (!hasAsset) {
      throw new Error('Asset should exist after being set');
    }
    console.log('✓ Asset exists check passed');

    // Test getAssetCacheDataAsMemoryView
    const memoryView = loader.getAssetCacheDataAsMemoryView(testAssetName);
    
    if (memoryView === undefined) {
      throw new Error('Memory view should not be undefined for existing asset');
    }
    console.log('✓ Memory view is not undefined');

    // Check if it's a Uint8Array
    if (!(memoryView instanceof Uint8Array)) {
      throw new Error('Memory view should be a Uint8Array');
    }
    console.log('✓ Memory view is a Uint8Array');

    // Check size
    if (memoryView.length !== expectedSize) {
      throw new Error(`Expected size ${expectedSize}, got ${memoryView.length}`);
    }
    console.log(`✓ Memory view has correct size: ${memoryView.length} bytes`);

    // Check content
    const decoder = new TextDecoder();
    const retrievedContent = decoder.decode(memoryView);
    if (retrievedContent !== testContent) {
      throw new Error(`Content mismatch. Expected: "${testContent}", Got: "${retrievedContent}"`);
    }
    console.log(`✓ Content matches: "${retrievedContent}"`);

    // Test with non-existing asset
    const nonExistingMemoryView = loader.getAssetCacheDataAsMemoryView('non-existing-asset');
    if (nonExistingMemoryView !== undefined) {
      throw new Error('Memory view should be undefined for non-existing asset');
    }
    console.log('✓ Non-existing asset returns undefined as expected');

    // Test with binary data
    const binaryAssetName = 'binary-test.bin';
    const binaryContent = 'Binary content: \x00\x01\x02\x03\xFF\xFE';
    
    loader.setAsset(binaryAssetName, binaryContent);
    const binaryMemoryView = loader.getAssetCacheDataAsMemoryView(binaryAssetName);
    
    const expectedBinarySize = new TextEncoder().encode(binaryContent).length;
    if (binaryMemoryView.length !== expectedBinarySize) {
      throw new Error(`Binary data size mismatch. Expected: ${expectedBinarySize}, Got: ${binaryMemoryView.length}`);
    }
    console.log('✓ Binary data memory view works correctly');

    // Test comparison with getAsset method
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
    console.log('✓ getAssetCacheDataAsMemoryView consistent with getAsset');

    console.log('\n🎉 All mock tests passed!');
    console.log('\nExpected behavior verified:');
    console.log('✓ Method returns Uint8Array memory view for existing assets');
    console.log('✓ Method returns undefined for non-existing assets');
    console.log('✓ Handles both text and binary data correctly');
    console.log('✓ Consistent with existing getAsset method');
    console.log('\nThe actual WebAssembly implementation should behave the same way.');

  } catch (error) {
    console.error('\n❌ Mock test failed:', error.message);
    process.exit(1);
  }
}

// Show the expected C++ binding signature
console.log('Expected C++ Implementation:');
console.log('-'.repeat(40));
console.log(`
// In EMAssetResolver:
emscripten::val getCacheDataAsMemoryView(const std::string &asset_name) const {
  if (!cache.count(asset_name)) {
    return emscripten::val::undefined();
  }
  const AssetCacheEntry &entry = cache.at(asset_name);
  return emscripten::val(emscripten::typed_memory_view(entry.binary.size(), 
                                                       reinterpret_cast<const uint8_t*>(entry.binary.data())));
}

// In TinyUSDZLoaderNative:
emscripten::val getAssetCacheDataAsMemoryView(const std::string &name) const {
  return em_resolver_.getCacheDataAsMemoryView(name);
}

// In EMSCRIPTEN_BINDINGS:
.function("getAssetCacheDataAsMemoryView", &TinyUSDZLoaderNative::getAssetCacheDataAsMemoryView)
`);
console.log('-'.repeat(40));
console.log();

runMockTest();