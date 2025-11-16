# Grouped and Flattened MaterialX Parameter Export

This implementation adds support for both **grouped** and **flattened** OpenPBR parameter naming in JSON export through the ThreeJSMaterialExporter.

## Summary

Added a new `use_grouped_parameters` option to `ThreeJSMaterialExporter::ExportOptions` that controls how MaterialX (OpenPBR) parameters are structured in JSON output.

## Changes Made

### 1. Header File (`src/tydra/threejs-exporter.hh`)
- Added `bool use_grouped_parameters` to `ExportOptions` struct

### 2. Implementation (`src/tydra/threejs-exporter.cc`)
- Added `setJsonParameter()` helper function for parameter name transformation
- Updated `ConvertOpenPBRToNodeMaterial()` to support both formats
- Updated `ConvertOpenPBRToPhysicalMaterial()` signature for consistency
- Updated `ExportMaterial()` to pass options through

## Output Formats

### Flattened Format (use_grouped_parameters = false)
All parameters at the same level with underscore-separated names:
```json
{
  "inputs": {
    "base_color": [0.8, 0.2, 0.1],
    "base_weight": 1.0,
    "base_roughness": 0.5,
    "base_metalness": 0.0,
    "specular_weight": 1.0,
    "specular_color": [1.0, 1.0, 1.0],
    "specular_ior": 1.5,
    ...
  }
}
```

### Grouped Format (use_grouped_parameters = true)
Parameters organized into logical groups with shortened property names:
```json
{
  "inputs": {
    "base": {
      "color": [0.8, 0.2, 0.1],
      "weight": 1.0,
      "roughness": 0.5,
      "metalness": 0.0
    },
    "specular": {
      "weight": 1.0,
      "color": [1.0, 1.0, 1.0],
      "ior": 1.5,
      ...
    },
    "coat": {...},
    "emission": {...},
    ...
  }
}
```

## Parameter Groups

The grouped format organizes parameters into:
- **base**: color, weight, roughness, metalness
- **specular**: color, weight, ior, roughness, anisotropy, rotation, ior_level
- **transmission**: color, weight, depth, scatter, scatter_anisotropy, dispersion
- **coat**: color, weight, roughness, ior, anisotropy, rotation, affect_color, affect_roughness
- **emission**: color, luminance
- **subsurface**: color, weight, radius, scale, anisotropy
- **sheen**: color, weight, roughness
- **geometry params**: opacity, normal, tangent (kept at root level)

## Usage Example

```cpp
#include "tydra/threejs-exporter.hh"

ThreeJSMaterialExporter exporter;
ThreeJSMaterialExporter::ExportOptions options;

// Export with flattened parameters (default)
options.use_grouped_parameters = false;
json flattened_output;
exporter.ExportMaterial(material, options, flattened_output);

// Export with grouped parameters
options.use_grouped_parameters = true;
json grouped_output;
exporter.ExportMaterial(material, options, grouped_output);
```

## Testing

Run the test to verify both formats:
```bash
cd tests/feat/mtlx
make -f Makefile.grouped_params
./test_grouped_params
```

## Comparison with material-serializer.cc

Note that `material-serializer.cc` (used by the WASM bindings and `dump-materialx-cli.js`) has its own grouped format that uses full parameter names within groups:

```json
{
  "base": {
    "base_weight": {"name": "base_weight", "type": "value", "value": 1.0},
    "base_color": {"name": "base_color", "type": "value", "value": [0.8, 0.2, 0.1]},
    ...
  }
}
```

The ThreeJSMaterialExporter provides a more compact grouped format suitable for Three.js node materials and WebGPU rendering.

## Backward Compatibility

- Default behavior is **flattened** (`use_grouped_parameters = false`)
- Existing code continues to work without modification
- Grouped format is opt-in via the options flag
