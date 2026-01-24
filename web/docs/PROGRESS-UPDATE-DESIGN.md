# JS/WASM Synchronous Progress Update Design

## Overview

This document describes design options for synchronous progress reporting from C++/WASM to JavaScript without using ASYNCIFY. The goal is to enable interactive progress updates during Tydra scene conversion.

## Problem Statement

The current implementation uses `printf()` statements in C++ (render-data.cc) which output to the browser console synchronously. However, there's no mechanism to:
1. Intercept these messages in JavaScript
2. Parse progress information
3. Update UI elements in real-time

## Key Insight: Emscripten printf() Behavior

Based on research ([Emscripten Module documentation](https://emscripten.org/docs/api_reference/module.html)):

- `printf()` in WASM outputs to console.log (line-buffered, needs `\n`)
- Output goes through `Module['print']` (stdout) or `Module['printErr']` (stderr)
- `Module['print']` can be overridden **before** module initialization
- Emscripten provides low-level functions: `emscripten_out()`, `emscripten_err()`, `emscripten_console_log()`

## Design Options

### Option 1: Module.print Override (Recommended)

**Approach**: Override `Module['print']` to intercept and parse progress messages from C++.

**Pros**:
- No C++ changes required (already using printf)
- Works with existing progress logging
- Zero overhead when not monitoring
- Clean separation of concerns

**Cons**:
- Requires parsing string output
- Must define Module before WASM loads

**Implementation**:

```javascript
// In pre-module-init code (before WASM loads)
const progressParser = {
  onProgress: null,
  totalMeshes: 0,
  currentMesh: 0
};

// Pattern: [Tydra] Mesh 3/10: /root/mesh_003
const MESH_PROGRESS_REGEX = /\[Tydra\] Mesh (\d+)\/(\d+)(?:\s*\([^)]+\))?:\s*(.+)/;
const COUNT_REGEX = /\[Tydra\] Found (\d+) meshes/;
const COMPLETE_REGEX = /\[Tydra\] Conversion complete/;

function createModuleWithProgressCapture(onProgress) {
  progressParser.onProgress = onProgress;

  return {
    print: function(text) {
      console.log(text);  // Keep console output

      // Parse progress messages
      let match;
      if ((match = MESH_PROGRESS_REGEX.exec(text))) {
        const current = parseInt(match[1]);
        const total = parseInt(match[2]);
        const meshName = match[3];
        onProgress({
          type: 'mesh',
          current,
          total,
          name: meshName,
          percentage: (current / total) * 100
        });
      } else if ((match = COUNT_REGEX.exec(text))) {
        progressParser.totalMeshes = parseInt(match[1]);
        onProgress({
          type: 'count',
          total: progressParser.totalMeshes
        });
      } else if (COMPLETE_REGEX.test(text)) {
        onProgress({
          type: 'complete'
        });
      }
    },
    printErr: function(text) {
      console.error(text);
    }
  };
}

// Usage in TinyUSDZLoader.js
const Module = createModuleWithProgressCapture((progress) => {
  updateProgressBar(progress.percentage);
  updateMeshStatus(`${progress.current}/${progress.total}: ${progress.name}`);
});
```

**Integration with existing code**:

```javascript
// progress-demo.js modification
async function loadUSDWithProgress(url) {
  // Create loader with progress capture
  loaderState.loader = new TinyUSDZLoader({
    moduleOverrides: {
      print: (text) => {
        console.log(text);
        parseTydraProgress(text);
      }
    }
  });
  // ... rest of loading code
}
```

---

### Option 2: EM_ASM Direct JavaScript Call

**Approach**: Call JavaScript directly from C++ using `EM_ASM` macro.

**Pros**:
- Direct, typed communication
- No string parsing required
- Can call any JavaScript function

**Cons**:
- Requires C++ code changes
- Adds emscripten-specific code to core library
- Compile-time dependency on emscripten headers

**Implementation**:

```cpp
// In render-data.cc (or a wrapper layer)
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void ReportMeshProgressToJS(size_t current, size_t total, const char* meshName) {
  EM_ASM({
    if (typeof Module.onMeshProgress === 'function') {
      Module.onMeshProgress($0, $1, UTF8ToString($2));
    }
  }, current, total, meshName);
}
#else
void ReportMeshProgressToJS(size_t, size_t, const char*) {}
#endif

// Call from MeshVisitor:
ReportMeshProgressToJS(meshes_processed, meshes_total, path.c_str());
```

```javascript
// JavaScript side
const Module = {
  onMeshProgress: function(current, total, name) {
    updateProgressUI(current, total, name);
  }
};
```

---

### Option 3: EM_JS Function Declaration

**Approach**: Declare JavaScript function inline in C++ header.

**Pros**:
- Cleaner than EM_ASM
- Type-safe function signature
- Compiled once

**Cons**:
- Same as Option 2 (requires C++ changes)

**Implementation**:

```cpp
// binding.cc or progress-bridge.h
#include <emscripten/em_js.h>

EM_JS(void, reportMeshProgress, (int current, int total, const char* name), {
  if (Module.onMeshProgress) {
    Module.onMeshProgress(current, total, UTF8ToString(name));
  }
});

// Use in render-data.cc via extern declaration or header include
extern "C" void reportMeshProgress(int current, int total, const char* name);
```

---

### Option 4: Shared Memory Buffer

**Approach**: Use a shared memory region that C++ writes to and JS polls.

**Pros**:
- No function call overhead
- Works with SharedArrayBuffer for workers
- Can be polled at any rate

**Cons**:
- Requires polling from JS side
- More complex synchronization
- SharedArrayBuffer requires specific headers

**Implementation**:

```cpp
// C++ side
struct ProgressBuffer {
  uint32_t currentMesh;
  uint32_t totalMeshes;
  uint32_t flags;  // bit 0: updated, bit 1: complete
  char meshName[256];
};

// Exposed via embind
ProgressBuffer* getProgressBuffer();
```

```javascript
// JS side - poll during animation frame
function pollProgress() {
  const buffer = Module.getProgressBuffer();
  if (buffer.flags & 1) {  // updated flag
    updateUI(buffer.currentMesh, buffer.totalMeshes, buffer.meshName);
    buffer.flags &= ~1;  // clear flag
  }
  if (!(buffer.flags & 2)) {  // not complete
    requestAnimationFrame(pollProgress);
  }
}
```

---

## Recommendation

**Option 1 (Module.print Override)** is recommended because:

1. **Zero C++ changes**: Current printf statements already work
2. **Non-invasive**: Doesn't pollute core library with emscripten code
3. **Flexible**: Can enable/disable progress capture per-load
4. **Maintainable**: All JS-specific code stays in JS layer
5. **Proven pattern**: Used by many emscripten projects

## Implementation Plan

### Phase 1: Basic Module.print Override
1. Modify TinyUSDZLoader.js to accept `moduleOverrides` option
2. Create progress message parser utility
3. Connect parser to progress UI in progress-demo.js

### Phase 2: Structured Progress Messages (Optional Enhancement)
1. Standardize printf format in render-data.cc with JSON-parseable structure
2. Example: `[TYDRA:PROGRESS] {"type":"mesh","current":3,"total":10,"name":"/root/mesh"}`
3. Makes parsing more robust

### Phase 3: Progress Event System
1. Create EventEmitter-style interface for progress events
2. Support multiple listeners
3. Add batch/debounce option for high-frequency updates

## Current printf Format Reference

From render-data.cc:
```
[Tydra] Counting primitives...
[Tydra] Found 10 meshes (8 mesh, 1 cube, 1 sphere), 3 materials
[Tydra] Mesh 1/10: /root/Mesh_001
[Tydra] Mesh 2/10: /root/Mesh_002
[Tydra] Mesh 3/10 (cube): /root/Cube_001
[Tydra] Mesh 4/10 (sphere): /root/Sphere_001
[Tydra] Conversion complete: 10 meshes, 3 materials, 5 textures
```

## References

- [Emscripten Module object documentation](https://emscripten.org/docs/api_reference/module.html)
- [Emscripten console.h documentation](https://emscripten.org/docs/api_reference/console.h.html)
- [GitHub Discussion: How does emscripten_console_xxx work?](https://github.com/emscripten-core/emscripten/discussions/21783)
