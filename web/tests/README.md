# TinyUSDZ Web Tests

This directory contains Node.js tests for the TinyUSDZ WebAssembly bindings.

## Tests

### test-memory-view.js

Tests the `getAssetCacheDataAsMemoryView` method which provides direct memory access to cached asset data.

**What it tests:**
- Returns `Uint8Array` memory view for existing assets
- Returns `undefined` for non-existing assets
- Handles both text and binary data correctly
- Consistent with existing `getAsset` method
- Proper data integrity and size validation

### test-zero-copy-mock.js

Tests the `setAssetFromRawPointer` method which enables zero-copy transfer of `Uint8Array` data from JavaScript to C++.

**What it tests:**
- Zero-copy data transfer using raw pointers
- Performance comparison with traditional method
- Data integrity verification
- Memory efficiency improvements
- Error handling for edge cases

**Performance Benefits:**
- Eliminates intermediate copying during data transfer
- Direct pointer access in C++ code
- Up to 67% reduction in memory copies
- Significant performance improvement for large assets

### test-value-clip.js

Tests USD value clip loading and retime behavior in the WebAssembly API.

**What it tests:**
- `setEnableValueClips` + `loadFromCachedAsset` composition path
- `hasValueClip` / `valueClipBaked` / `clipAssetPaths` metadata
- `setValueClipSampleRate`, `setValueClipUseTimeRange`, and `setValueClipTimeRange`
- value-clip enable/disable behavior and time resampling impact

## Running Tests

### Prerequisites

1. Build the TinyUSDZ WebAssembly module first:
   ```bash
   cd ../
   ./bootstrap-linux.sh
   cd build && make
   ```

2. Make sure the generated files are available at `../js/src/tinyusdz/`

### Run Tests

```bash
# Run all tests (mock versions)
npm test

# Run specific tests
npm run test-memory-view    # Actual WebAssembly test
npm run test-zero-copy      # Zero-copy mock test
npm run test-value-clip    # Value clip WebAssembly test
npm run test-mock          # All mock tests
```

Or directly with Node.js:

```bash
node test-memory-view.js        # Requires built WebAssembly module
node test-zero-copy-mock.js     # Mock test, no build required
node test-value-clip.js         # Value clip WebAssembly test
```

## Test Structure

Each test file:
- Loads the TinyUSDZ WebAssembly module
- Creates test scenarios with various data types
- Validates method behavior and edge cases
- Reports results clearly with ✓/❌ indicators

## Utilities

### zero-copy-utils.js

Helper functions for using the zero-copy functionality in real applications.

**Functions:**
- `setAssetZeroCopy()` - High-level helper for zero-copy asset setting
- `loadFileAsAssetZeroCopy()` - Load file directly with zero-copy
- `getPointerFromUint8Array()` - Get raw pointer from Uint8Array
- `comparePerformance()` - Benchmark traditional vs zero-copy methods
- `validateUint8Array()` - Validate compatibility for zero-copy

**Usage:**
```javascript
const utils = require('./zero-copy-utils.js');

// Simple zero-copy asset setting
const success = utils.setAssetZeroCopy(Module, loader, 'texture.jpg', uint8Array);

// Load file with zero-copy
await utils.loadFileAsAssetZeroCopy(Module, loader, 'model.usd', 'path/to/file.usd');
```

## Adding New Tests

When adding new tests:
1. Create a new `.js` file in this directory
2. Follow the existing test pattern
3. Add a script entry in `package.json`
4. Update this README with test description
