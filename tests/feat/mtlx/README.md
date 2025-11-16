# MaterialX Export Feature Tests

This directory contains tests for the MaterialX XML export functionality in TinyUSDZ.

## Overview

The MaterialX export feature allows converting TinyUSDZ materials (particularly OpenPBR Surface Shader) to MaterialX 1.38 XML format, which is compatible with Three.js WebGPU renderer and other MaterialX-compliant renderers.

## Building and Running Tests

### Prerequisites

1. Build the main TinyUSDZ library first:
   ```bash
   cd ../../../
   mkdir -p build && cd build
   cmake ..
   make
   ```

### Build the Test

From this directory:
```bash
make
```

### Run the Test

```bash
make run
```

This will:
- Create a RenderMaterial with OpenPBR shader properties
- Export it to MaterialX XML format
- Save the output to `example_openpbr_material.mtlx`
- Display the generated XML

### Clean Build Artifacts

```bash
make clean
```

### View Help

```bash
make help
```

## Test Files

- **threejs_mtlx_export_example.cc** - Demonstrates MaterialX export API usage
- **Makefile** - Standalone build configuration
- **README.md** - This file

## Expected Output

The test creates a MaterialX 1.38 XML document with:
- `<open_pbr_surface>` shader node with all OpenPBR parameters
- `<surfacematerial>` that references the shader
- Proper type declarations (float, color3, vector3)

Example material properties tested:
- Base layer (metallic gold-like material)
- Specular reflections
- Clear coat layer
- Emission settings
- Geometry modifiers (normal, tangent, opacity)

## Integration

This export functionality is part of the Three.js material exporter in:
- `src/tydra/threejs-exporter.hh`
- `src/tydra/threejs-exporter.cc`

The API is simple:
```cpp
ThreeJSMaterialExporter exporter;
std::string mtlx_output;
bool success = exporter.ExportMaterialX(material, mtlx_output);
```

## MaterialX Schema Support

Currently supports:
- ✅ OpenPBR Surface Shader → `<open_pbr_surface>` (MaterialX 1.38)
- ✅ UsdPreviewSurface → `<standard_surface>` (fallback)
- ✅ All 35+ OpenPBR parameters
- ✅ Texture node references
- ✅ Constant value exports

## Further Reading

- [MaterialX Specification](https://www.materialx.org/)
- [OpenPBR Surface Documentation](https://academysoftwarefoundation.github.io/OpenPBR/)
- [Three.js MaterialX Support](https://threejs.org/)
