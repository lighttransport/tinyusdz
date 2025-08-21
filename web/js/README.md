# TinyUSDZ JS/WASM module development(for developer)

## Requrements

* bun

## Setup

```
$ bun install
```

### Assets

Copy assets folder from demo directory by running `setup-assets.sh`

### Run

```
$ bun run dev
```

## Memory Limit Configuration

TinyUSDZLoader now supports memory limit configuration to prevent memory exhaustion attacks when loading potentially malicious USD files.

### Usage

```javascript
import { TinyUSDZLoader } from 'tinyusdz/TinyUSDZLoader.js';

// Option 1: Set memory limit in constructor
// (Default: 2GB for WASM32, 8GB for WASM64)
const loader = new TinyUSDZLoader(null, { maxMemoryLimitMB: 512 });

// Option 2: Set memory limit after creation
const loader2 = new TinyUSDZLoader();
loader2.setMaxMemoryLimitMB(1024); // Set 1GB limit

// Option 3: Override memory limit for specific load operations
loader.load(url, onLoad, onProgress, onError, { maxMemoryLimitMB: 256 });

// Option 4: Check the native default memory limit
const defaultLimit = await loader.getNativeDefaultMemoryLimitMB();
console.log(`Native default memory limit: ${defaultLimit} MB`);
```

### Security Considerations

- Default memory limits:
  - **2GB for 32-bit WASM** (standard build)
  - **8GB for 64-bit WASM** (MEMORY64 build)
- Memory limits are enforced at the WASM level during USD parsing
- Lower limits are recommended for untrusted USD files
- Memory limit applies to both Stage and Layer loading operations

## NPM packaging

NPM packaing is not handled in this folder.

Please see `../npm`


