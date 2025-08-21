# JavaScript Scripting Example

This example demonstrates how to load a USD file as a Layer in C++ and query LayerMeta information through the Tydra JavaScript interface.

## Overview

The example shows how to:
1. Load a USD file using TinyUSDZ's C++ API
2. Access the LayerMetas from the loaded Stage
3. Execute JavaScript code that can query layer metadata through the Tydra interface
4. Use the `getLayerMetas()` function from JavaScript to access USD layer properties

## Requirements

- TinyUSDZ built with QuickJS support (`-DTINYUSDZ_WITH_QJS=ON`)
- A USD file to query (sample provided)

## Building

From the TinyUSDZ root directory:

```bash
mkdir build && cd build
cmake -DTINYUSDZ_WITH_QJS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON ..
make js-script
```

## Usage

```bash
./js-script <USD_FILE> <JAVASCRIPT_FILE>
```

### Example Usage

```bash
# Use the provided sample files
./js-script examples/js-script/sample.usda examples/js-script/query_layer.js

# Or use your own USD file
./js-script path/to/your/model.usd query_layer.js
```

## Files

- **`main.cc`** - C++ application that loads USD and executes JavaScript
- **`query_layer.js`** - Example JavaScript that demonstrates LayerMetas querying
- **`sample.usda`** - Sample USD file with various metadata for testing
- **`CMakeLists.txt`** - Build configuration

## JavaScript API

The JavaScript environment provides access to:

### `getLayerMetas()`

Returns a JavaScript object containing USD layer metadata:

```javascript
var layerMetas = getLayerMetas();
console.log("Up Axis: " + layerMetas.upAxis);
console.log("Default Prim: " + layerMetas.defaultPrim);
console.log("Meters Per Unit: " + layerMetas.metersPerUnit);
```

### Available LayerMetas Properties

- `upAxis` - Coordinate system up axis ("X", "Y", or "Z")
- `defaultPrim` - Name of the default prim
- `metersPerUnit` - Scale conversion factor
- `timeCodesPerSecond` - Time sampling rate
- `framesPerSecond` - Animation frame rate
- `startTimeCode` - Animation start time
- `endTimeCode` - Animation end time (null if infinite)
- `kilogramsPerUnit` - Mass unit conversion
- `comment` - Layer comment string
- `doc` - Layer documentation string
- `autoPlay` - USDZ auto-play setting
- `playbackMode` - USDZ playback mode
- `subLayers` - Array of sub-layer references
- `primChildren` - Array of root-level prim names

### Example Output

```
=== USD Layer Metadata Query Script ===
LayerMetas successfully retrieved!

=== Basic Layer Metadata ===
Up Axis: Y
Default Prim: Scene
Meters Per Unit: 1
Time Codes Per Second: 24
Frames Per Second: 24

=== Time Range ===
Start Time Code: 1
End Time Code: 100

=== Root Prim Children ===
Number of root prims: 1
  Prim 0: Scene

=== Analysis ===
Has default prim: true
Has time range: true
Is animated: true
```

## Extending the Example

You can create your own JavaScript files to:

1. **Analyze USD structure** - Check metadata patterns, validate settings
2. **Report generation** - Extract metadata for external tools
3. **Conditional processing** - Make decisions based on USD properties
4. **Validation** - Verify USD files meet specific requirements

### Custom JavaScript Example

```javascript
// Custom validation script
var layerMetas = getLayerMetas();

// Check for required metadata
if (!layerMetas.defaultPrim) {
    console.log("WARNING: No default prim set");
}

if (layerMetas.upAxis !== "Y") {
    console.log("INFO: Using " + layerMetas.upAxis + " as up-axis");
}

// Generate report
console.log("=== USD File Report ===");
console.log("File appears to be: " + 
    (layerMetas.endTimeCode > layerMetas.startTimeCode ? "Animated" : "Static"));
console.log("Scale: " + layerMetas.metersPerUnit + " meters per unit");
console.log("Frame rate: " + layerMetas.framesPerSecond + " fps");
```

## Technical Notes

- The example uses QuickJS for JavaScript execution
- LayerMetas are converted to JSON and parsed in the JavaScript context
- The C++ side sets up a global `getLayerMetas()` function accessible from JavaScript
- JavaScript execution is sandboxed and stateless
- All USD data types are converted to appropriate JavaScript types

## Troubleshooting

**Error: "JavaScript is not supported in this build"**
- Rebuild TinyUSDZ with `-DTINYUSDZ_WITH_QJS=ON`

**Error: "Failed to load USD file"**
- Check that the USD file path is correct
- Verify the USD file is valid

**Error: "Failed to load JavaScript file"**
- Check that the JavaScript file path is correct
- Verify the JavaScript file contains valid JavaScript code