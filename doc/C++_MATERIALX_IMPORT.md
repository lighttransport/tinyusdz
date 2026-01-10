# C++ MaterialX Import Support

## Overview

TinyUSDZ now includes built-in C++ support for loading MaterialX (.mtlx) files without external dependencies. The implementation uses a secure, dependency-free XML parser specifically designed for MaterialX documents.

## Architecture

### Components

1. **MaterialX Parser** (`src/mtlx-*.hh/cc`)
   - `mtlx-xml-tokenizer`: Low-level XML tokenization with security limits
   - `mtlx-simple-parser`: Lightweight DOM tree builder
   - `mtlx-dom`: MaterialX-specific document object model
   - `mtlx-usd-adapter`: pugixml-compatible interface

2. **USD Integration** (`src/usdMtlx.cc/hh`)
   - MaterialX to USD conversion
   - Support for multiple shader types
   - PrimSpec generation

### Supported Shaders

- ✅ **OpenPBR Surface** (`open_pbr_surface`) - Full support
- ✅ **Autodesk Standard Surface** (`standard_surface`) - Full support
- ✅ **USD Preview Surface** (`UsdPreviewSurface`) - Full support

## API Usage

### Basic Import

```cpp
#include "usdMtlx.hh"

// Load from string
std::string xml_content = "...";
tinyusdz::MtlxModel mtlx;
std::string warn, err;

bool success = tinyusdz::ReadMaterialXFromString(
    xml_content,
    "material.mtlx",  // asset name
    &mtlx,
    &warn,
    &err
);

if (success) {
    std::cout << "Loaded MaterialX version: " << mtlx.version << std::endl;
}
```

### Load from File

```cpp
#include "usdMtlx.hh"

tinyusdz::AssetResolutionResolver resolver;
tinyusdz::MtlxModel mtlx;
std::string warn, err;

bool success = tinyusdz::ReadMaterialXFromFile(
    resolver,
    "path/to/material.mtlx",
    &mtlx,
    &warn,
    &err
);
```

### Convert to USD PrimSpec

```cpp
// Convert MaterialX model to USD PrimSpec
tinyusdz::PrimSpec ps;
std::string err;

bool success = tinyusdz::ToPrimSpec(mtlx, ps, &err);

if (success) {
    // Use PrimSpec in USD Stage
    // ...
}
```

### Load as USD Asset Reference

```cpp
#include "usdMtlx.hh"

tinyusdz::Asset asset;
tinyusdz::PrimSpec ps;
std::string warn, err;

bool success = tinyusdz::LoadMaterialXFromAsset(
    asset,
    "material.mtlx",
    ps,  // inout parameter
    &warn,
    &err
);
```

## OpenPBR Surface Support

The `MtlxOpenPBRSurface` shader supports all OpenPBR specification parameters:

### Base Layer
- `base_weight` (float)
- `base_color` (color3)
- `base_metalness` (float)
- `base_diffuse_roughness` (float)

### Specular Layer
- `specular_weight` (float)
- `specular_color` (color3)
- `specular_roughness` (float)
- `specular_ior` (float)
- `specular_anisotropy` (float)
- `specular_rotation` (float)

### Transmission
- `transmission_weight` (float)
- `transmission_color` (color3)
- `transmission_depth` (float)
- `transmission_scatter` (color3)
- `transmission_scatter_anisotropy` (float)
- `transmission_dispersion` (float)

### Subsurface
- `subsurface_weight` (float)
- `subsurface_color` (color3)
- `subsurface_radius` (color3)
- `subsurface_scale` (float)
- `subsurface_anisotropy` (float)

### Coat (Clearcoat)
- `coat_weight` (float)
- `coat_color` (color3)
- `coat_roughness` (float)
- `coat_anisotropy` (float)
- `coat_rotation` (float)
- `coat_ior` (float)
- `coat_affect_color` (float)
- `coat_affect_roughness` (float)

### Thin Film
- `thin_film_thickness` (float)
- `thin_film_ior` (float)

### Emission
- `emission_luminance` (float)
- `emission_color` (color3)

### Geometry
- `geometry_opacity` (float)
- `geometry_thin_walled` (bool)
- `geometry_normal` (normal3)
- `geometry_tangent` (vector3)

## Example MaterialX File

```xml
<?xml version="1.0"?>
<materialx version="1.38">
  <surfacematerial name="RedMetal" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="RedMetal_shader" />
  </surfacematerial>

  <open_pbr_surface name="RedMetal_shader" type="surfaceshader">
    <input name="base_color" type="color3" value="0.8, 0.2, 0.2" />
    <input name="base_weight" type="float" value="1.0" />
    <input name="base_metalness" type="float" value="0.8" />
    <input name="specular_roughness" type="float" value="0.3" />
    <input name="specular_ior" type="float" value="1.5" />
  </open_pbr_surface>
</materialx>
```

## Security Features

The built-in parser includes multiple security safeguards:

- **Maximum name length**: 256 characters
- **Maximum string length**: 64KB
- **Maximum text content**: 1MB
- **Maximum nesting depth**: 1000 levels
- **Safe entity handling**: HTML entities only (no external entity expansion)
- **No external file access**: Prevents XXE attacks
- **Memory limits**: Prevents denial-of-service attacks

## Build Configuration

### CMake

```cmake
# Enable MaterialX support (enabled by default)
set(TINYUSDZ_USE_USDMTLX ON CACHE BOOL "Enable MaterialX support")
```

### Compile Flags

```bash
# Enable MaterialX in your build
-DTINYUSDZ_USE_USDMTLX
```

## Testing

### Run Tests

```bash
cd tests/feat/mtlx
make clean
make
./test_mtlx_import
```

### Test with Custom File

```bash
./test_mtlx_import path/to/your/material.mtlx
```

### Expected Output

```
=== TinyUSDZ MaterialX Import Test ===

Test 1: Parsing MaterialX XML from string...
✓ Successfully parsed MaterialX

Parsed MaterialX information:
  Asset name: test.mtlx
  Version: 1.38
  Shader name: TestMaterial_shader
  Surface materials: 1
  Shaders: 1

Test 2: Converting MaterialX to USD PrimSpec...
✓ Successfully converted to PrimSpec
  PrimSpec name: TestMaterial
  PrimSpec type: Material

=== All tests passed! ===
```

## Comparison: pugixml vs Built-in Parser

| Feature | pugixml | Built-in Parser |
|---------|---------|-----------------|
| **External Dependency** | Yes | No |
| **Size** | ~200KB | Integrated |
| **Security** | Basic | Enhanced |
| **Memory Limits** | Manual | Automatic |
| **MaterialX Specific** | No | Yes |
| **XXE Protection** | Manual | Built-in |
| **Performance** | Fast | Fast |

## Migration from pugixml

The migration is automatic - the built-in parser provides a pugixml-compatible adapter:

**Before** (with external pugixml):
```cpp
#include "external/pugixml.hpp"
pugi::xml_document doc;
pugi::xml_parse_result result = doc.load_string(xml);
```

**After** (with built-in parser):
```cpp
#include "mtlx-usd-adapter.hh"
tinyusdz::mtlx::pugi::xml_document doc;
tinyusdz::mtlx::pugi::xml_parse_result result = doc.load_string(xml);
```

The API is compatible, so existing code continues to work.

## Limitations

- **MaterialX versions**: Supports 1.36, 1.37, and 1.38
- **XML namespaces**: Basic support (MaterialX doesn't use them heavily)
- **XPath**: Not supported (not needed for MaterialX)
- **DOM manipulation**: Read-only parsing

## Error Handling

```cpp
std::string warn, err;
bool success = tinyusdz::ReadMaterialXFromString(xml, name, &mtlx, &warn, &err);

if (!success) {
    std::cerr << "Error: " << err << std::endl;
    return 1;
}

if (!warn.empty()) {
    std::cout << "Warnings: " << warn << std::endl;
}
```

## Future Enhancements

- [ ] MaterialX node graph support beyond surface shaders
- [ ] MaterialX standard library includes
- [ ] Write support (currently read-only)
- [ ] XPath queries for advanced filtering
- [ ] Texture node parsing and loading
- [ ] MaterialX validation against schema

## References

- [MaterialX Specification](https://materialx.org/)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [Autodesk Standard Surface](https://github.com/Autodesk/standard-surface)
- [USD Specification](https://openusd.org/)

## License

Apache 2.0 - Same as TinyUSDZ project

## Contact

- Issues: https://github.com/lighttransport/tinyusdz/issues
- Discussions: https://github.com/lighttransport/tinyusdz/discussions
