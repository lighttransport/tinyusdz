# MaterialX Support in TinyUSDZ

This document describes the MaterialX integration, color space support, and implementation roadmap for complete MaterialX support in TinyUSDZ.

## Overview

TinyUSDZ provides comprehensive support for MaterialX, including a full suite of color space conversions required for proper MaterialX document processing. The library can parse MaterialX (.mtlx) files and handle all standard MaterialX color spaces. This document also outlines the current state of MaterialX support and provides a comprehensive todo list for complete MaterialX and MaterialXConfigAPI implementation in both the core library and Tydra render material conversion pipeline.

## Color Space Support

### Supported Color Spaces

TinyUSDZ supports all major color spaces used in MaterialX documents:

| Color Space | Enum Value | Description |
|------------|------------|-------------|
| `srgb` | `ColorSpace::sRGB` | Standard RGB with sRGB transfer function |
| `lin_srgb` | `ColorSpace::Lin_sRGB` | Linear sRGB (no gamma) |
| `srgb_texture` | `ColorSpace::sRGB_Texture` | sRGB for texture inputs |
| `rec709` | `ColorSpace::Rec709` | Rec.709 with gamma |
| `lin_rec709` | `ColorSpace::Lin_Rec709` | Linear Rec.709 (MaterialX default) |
| `g22_rec709` | `ColorSpace::g22_Rec709` | Rec.709 with gamma 2.2 |
| `g18_rec709` | `ColorSpace::g18_Rec709` | Rec.709 with gamma 1.8 |
| `lin_rec2020` | `ColorSpace::Lin_Rec2020` | Linear Rec.2020/Rec.2100 |
| `acescg` / `lin_ap1` | `ColorSpace::Lin_ACEScg` | ACES CG (AP1 primaries) |
| `aces2065-1` | `ColorSpace::ACES2065_1` | ACES 2065-1 (AP0 primaries) |
| `lin_displayp3` | `ColorSpace::Lin_DisplayP3` | Linear Display P3 |
| `srgb_displayp3` | `ColorSpace::sRGB_DisplayP3` | Display P3 with sRGB transfer |
| `raw` | `ColorSpace::Raw` | No color space (data textures) |

### Color Space Conversion Functions

#### sRGB Conversions
```cpp
// 8-bit sRGB ↔ Linear conversions
bool srgb_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);
bool linear_f32_to_srgb_8bit(const std::vector<float> &in_img, ...);

// Float32 sRGB ↔ Linear conversions  
bool srgb_f32_to_linear_f32(const std::vector<float> &in_img, ...);
```

#### Rec.709 Conversions
```cpp
// Rec.709 with standard gamma
bool rec709_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);

// Note: lin_rec709 has the same primaries as sRGB/Rec.709, 
// so no color space conversion is needed, only gamma
```

#### Rec.2020 Conversions
```cpp
// Rec.2020 gamma ↔ linear conversions
bool rec2020_8bit_to_linear_f32(const std::vector<uint8_t> &in_img, ...);
bool linear_f32_to_rec2020_8bit(const std::vector<float> &in_img, ...);

// Rec.2020 ↔ sRGB color gamut conversions
bool linear_rec2020_to_linear_sRGB(const std::vector<float> &in_img, ...);
bool linear_sRGB_to_linear_rec2020(const std::vector<float> &in_img, ...);
```

#### Gamma Conversions
```cpp
// Gamma 2.2 conversions (for g22_rec709)
bool gamma22_f32_to_linear_f32(const std::vector<float> &in_img, ...);
bool linear_f32_to_gamma22_f32(const std::vector<float> &in_img, ...);

// Gamma 1.8 conversions (for g18_rec709)
bool gamma18_f32_to_linear_f32(const std::vector<float> &in_img, ...);
bool linear_f32_to_gamma18_f32(const std::vector<float> &in_img, ...);
```

#### ACES Conversions
```cpp
// ACEScg (AP1) conversions
bool linear_sRGB_to_ACEScg(const std::vector<float> &in_img, ...);
bool ACEScg_to_linear_sRGB(const std::vector<float> &in_img, ...);

// ACES 2065-1 (AP0) conversions
bool linear_sRGB_to_ACES2065_1(const std::vector<float> &in_img, ...);
bool ACES2065_1_to_linear_sRGB(const std::vector<float> &in_img, ...);
```

#### Display P3 Conversions
```cpp
// Display P3 conversions
bool linear_displayp3_to_linear_sRGB(const std::vector<float> &in_img, ...);
bool linear_sRGB_to_linear_displayp3(const std::vector<float> &in_img, ...);
bool displayp3_f16_to_linear_f32(const std::vector<value::half> &in_img, ...);
```

## MaterialX Integration

### MaterialX Parser

TinyUSDZ includes a MaterialX parser located in `sandbox/mtlx-parser/` that can:
- Parse MaterialX XML documents (.mtlx files)
- Extract document-level colorspace settings
- Parse element-level colorspace attributes
- Handle MaterialX node graphs and material definitions

### Color Space in MaterialX Files

MaterialX files typically specify color spaces at multiple levels:

1. **Document Level**: Set in the root `<materialx>` element
   ```xml
   <materialx version="1.38" colorspace="lin_rec709">
   ```

2. **Texture Level**: Specified on `<image>` and `<tiledimage>` nodes
   ```xml
   <image name="diffuse_tex" type="color3" colorspace="srgb_texture">
   ```

3. **Value Level**: Can be specified on individual inputs
   ```xml
   <input name="opacity" type="float" value="0.5" colorspace="lin_rec709"/>
   ```

### Usage Example

```cpp
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "image-util.hh"

// Load a USD file with MaterialX materials
tinyusdz::Stage stage;
std::string warn, err;
bool ret = tinyusdz::LoadUSDFromFile("model_with_mtlx.usd", &stage, &warn, &err);

// The color space is automatically inferred from MaterialX metadata
tinyusdz::tydra::ColorSpace colorSpace;
tinyusdz::value::token colorSpaceToken("lin_rec709");
if (tinyusdz::tydra::InferColorSpace(colorSpaceToken, &colorSpace)) {
    // colorSpace is now ColorSpace::Lin_Rec709
}

// Convert textures to the appropriate color space
std::vector<uint8_t> srgb_texture_data = LoadTexture("diffuse.png");
std::vector<float> linear_data;

// Convert from sRGB texture space to linear for rendering
tinyusdz::srgb_8bit_to_linear_f32(
    srgb_texture_data, 
    width, height, 
    3, 3,  // RGB channels
    &linear_data
);
```

## Implementation Details

### Color Space Matrices

The color space conversions use standard transformation matrices derived from the CIE chromaticity coordinates of each color space:

- **sRGB/Rec.709**: Standard D65 white point, ITU-R BT.709 primaries
- **Rec.2020**: D65 white point, ITU-R BT.2020 primaries  
- **Display P3**: D65 white point, DCI-P3 primaries adapted to D65
- **ACEScg (AP1)**: D60 white point, ACES AP1 primaries
- **ACES 2065-1 (AP0)**: D60 white point, ACES AP0 primaries

### Transfer Functions

The library implements the following transfer functions:

1. **sRGB Transfer Function**: 
   - Forward: Piecewise function with linear segment below 0.04045
   - Inverse: Piecewise function with linear segment below 0.0031308

2. **Rec.709 Transfer Function**:
   - Similar to sRGB but with slightly different parameters
   - Linear segment below 0.018 (β = 0.018054 for 10-bit)

3. **Rec.2020 Transfer Function**:
   - Uses the same OETF as Rec.709 with 10-bit quantization parameters

4. **Simple Gamma Functions**:
   - Gamma 2.2: `y = x^2.2` (decode), `y = x^(1/2.2)` (encode)
   - Gamma 1.8: `y = x^1.8` (decode), `y = x^(1/1.8)` (encode)

### Performance Optimizations

- **Lookup Tables**: sRGB conversions use pre-computed 256-entry LUTs for 8-bit data
- **SIMD Support**: Vector operations are used where available
- **In-place Operations**: Memory efficient implementations where possible

## Common MaterialX Workflows

### Loading MaterialX Textures

When loading textures referenced in MaterialX documents:

1. Check the `colorspace` attribute on the texture node
2. Load the raw texture data
3. Convert from the specified color space to linear (working space)
4. Apply any additional MaterialX color transformations

### Example: Processing a MaterialX Surface

```cpp
// Typical MaterialX standard_surface material workflow
void ProcessMaterialXSurface(const MaterialXSurface& mtlxSurf) {
    // Base color is usually in srgb_texture space
    std::vector<float> baseColorLinear;
    if (mtlxSurf.baseColorSpace == "srgb_texture") {
        srgb_8bit_to_linear_f32(
            mtlxSurf.baseColorTexture,
            width, height, 3, 3,
            &baseColorLinear
        );
    }
    
    // Normal maps are typically "raw" (no color space)
    // Roughness, metallic are also usually "raw"
    // These don't need color space conversion
    
    // Emission might be in a different space
    if (mtlxSurf.emissionColorSpace == "acescg") {
        // Convert from ACEScg to working space if needed
        ACEScg_to_linear_sRGB(...);
    }
}
```

## File Locations

- **Header**: `src/image-util.hh` - Color conversion function declarations
- **Implementation**: `src/image-util.cc` - Color conversion implementations
- **Tydra Integration**: `src/tydra/render-data.{hh,cc}` - ColorSpace enum and inference
- **MaterialX Parser**: `sandbox/mtlx-parser/` - MaterialX document parsing

## Testing

Color space conversions can be tested using:
```bash
# Build with tests enabled
cmake -DTINYUSDZ_BUILD_TESTS=ON ..
make

# Run unit tests
./test_tinyusdz

# Test with MaterialX files
./tydra_to_renderscene data/materialx/StandardSurface/standard_surface_default.mtlx
```

## Current Implementation Status

### ✅ Completed Features

1. **Basic MaterialX XML Parsing**
   - XML parser in `src/usdMtlx.cc` using pugixml
   - Secure MaterialX parser in `sandbox/mtlx-parser/` (dependency-free)
   - Support for MaterialX v1.36, v1.37, v1.38

2. **Color Space Support**
   - Complete color space conversion functions in `src/image-util.cc`
   - Support for all MaterialX color spaces (sRGB, lin_rec709, ACEScg, etc.)
   - Color space inference in Tydra (`InferColorSpace()`)

3. **Shader Definitions**
   - `MtlxUsdPreviewSurface` shader struct defined
   - `MtlxAutodeskStandardSurface` shader struct (partial)
   - `OpenPBRSurface` shader struct with all parameters

4. **Tydra Material Conversion**
   - `UsdPreviewSurface` → `PreviewSurfaceShader` conversion
   - `OpenPBRSurface` → `OpenPBRSurfaceShader` conversion

5. **MaterialXConfigAPI Structure**
   - Basic `MaterialXConfigAPI` struct in `src/usdShade.hh`
   - `mtlx_version` attribute support

### ⚠️ Partial Implementation

1. **MaterialX File Import**
   - Basic `.mtlx` file loading via references
   - Limited node graph support
   - No full composition support

2. **Material Reconstruction**
   - `UsdPreviewSurface` reconstruction works
   - No `OpenPBRSurface` reconstruction in `prim-reconstruct.cc`
   - No `MtlxAutodeskStandardSurface` reconstruction

## Implementation Todo List

### 1. Core MaterialX Support

#### 1.1 MaterialXConfigAPI Implementation
- [ ] **Parse MaterialXConfigAPI from USD files**
  - [ ] Add MaterialXConfigAPI parsing in `prim-reconstruct.cc`
  - [ ] Support `config:mtlx:version` attribute
  - [ ] Support `config:mtlx:namespace` attribute
  - [ ] Support `config:mtlx:colorspace` attribute

- [ ] **Extend MaterialXConfigAPI structure**
  ```cpp
  struct MaterialXConfigAPI {
    TypedAttributeWithFallback<std::string> mtlx_version{"1.38"};
    TypedAttributeWithFallback<std::string> mtlx_namespace{""};
    TypedAttributeWithFallback<std::string> mtlx_colorspace{"lin_rec709"};
    TypedAttributeWithFallback<std::string> mtlx_sourceUri{""};
  };
  ```

#### 1.2 Shader Reconstruction
- [ ] **Implement OpenPBRSurface reconstruction**
  - [ ] Add `ReconstructShader<OpenPBRSurface>()` template specialization
  - [ ] Parse all OpenPBR parameters from USD properties
  - [ ] Handle texture connections for OpenPBR inputs

- [ ] **Implement MtlxAutodeskStandardSurface reconstruction**
  - [ ] Complete the StandardSurface struct with all parameters
  - [ ] Add `ReconstructShader<MtlxAutodeskStandardSurface>()`
  - [ ] Parse all StandardSurface parameters

- [ ] **Implement MtlxOpenPBRSurface reconstruction**
  - [ ] Add `MtlxOpenPBRSurface` struct (MaterialX-specific variant)
  - [ ] Add reconstruction support

#### 1.3 MaterialX Node Graph Support
- [ ] **Parse NodeGraph prims**
  - [ ] Implement `NodeGraph` struct in `usdShade.hh`
  - [ ] Add NodeGraph reconstruction in `prim-reconstruct.cc`
  - [ ] Support nested node connections

- [ ] **Node Types Support**
  - [ ] Image nodes (`<image>`, `<tiledimage>`)
  - [ ] Math nodes (`<add>`, `<multiply>`, etc.)
  - [ ] Color transform nodes
  - [ ] Procedural nodes (`<noise2d>`, `<fractal3d>`, etc.)

### 2. MaterialX File Loading

#### 2.1 Enhanced MaterialX Parser
- [ ] **Extend MaterialX DOM**
  - [ ] Parse `<nodedef>` definitions
  - [ ] Parse `<nodegraph>` structures
  - [ ] Parse `<material>` elements
  - [ ] Parse `<look>` and `<collection>` elements

- [ ] **MaterialX Version Handling**
  - [ ] Auto-detect MaterialX version from document
  - [ ] Version-specific attribute handling
  - [ ] Upgrade paths for older versions

#### 2.2 Asset Resolution
- [ ] **MaterialX File References**
  - [ ] Support `.mtlx` file references in USD
  - [ ] Implement MaterialX library path resolution
  - [ ] Cache loaded MaterialX documents

- [ ] **Include and Library Support**
  - [ ] Parse `<xi:include>` directives
  - [ ] Support MaterialX standard libraries
  - [ ] Custom library path configuration

### 3. Tydra Render Material Conversion

#### 3.1 Material Conversion Pipeline
- [ ] **MaterialX → RenderMaterial conversion**
  - [ ] Add `ConvertMaterialXShader()` method
  - [ ] Map MaterialX nodes to RenderMaterial properties
  - [ ] Handle node graph evaluation

- [ ] **Shader Network Evaluation**
  - [ ] Implement node connection resolver
  - [ ] Support value inheritance and defaults
  - [ ] Handle interface tokens and bindings

#### 3.2 Texture and Image Handling
- [ ] **MaterialX Texture Support**
  - [ ] Parse `<image>` node parameters
  - [ ] Support `<tiledimage>` with UV transforms
  - [ ] Handle texture color space attributes
  - [ ] Support UDIM and texture arrays

- [ ] **Color Space Conversions**
  - [ ] Auto-convert textures based on MaterialX colorspace
  - [ ] Support per-channel color spaces
  - [ ] Handle HDR textures correctly

### 4. Advanced MaterialX Features

#### 4.1 Geometry and Collections
- [ ] **Geometry Assignment**
  - [ ] Parse `<geominfo>` elements
  - [ ] Support geometry collections
  - [ ] Handle per-face material assignments

- [ ] **Material Variants**
  - [ ] Parse `<variant>` elements
  - [ ] Support variant sets
  - [ ] Implement variant selection API

#### 4.2 Units and Physical Properties
- [ ] **Unit System Support**
  - [ ] Parse unit attributes
  - [ ] Implement unit conversions
  - [ ] Support scene scale factors

- [ ] **Physical Material Properties**
  - [ ] IOR databases
  - [ ] Physical measurement units
  - [ ] Energy conservation validation

### 5. Testing and Validation

#### 5.1 Test Infrastructure
- [ ] **Unit Tests**
  - [ ] MaterialXConfigAPI parsing tests
  - [ ] Shader reconstruction tests
  - [ ] Node graph parsing tests
  - [ ] Color space conversion tests

- [ ] **Integration Tests**
  - [ ] Load MaterialX example files
  - [ ] Round-trip USD → MaterialX → USD
  - [ ] Validate against MaterialX test suite

#### 5.2 Example Files
- [ ] **Create test scenes**
  - [ ] Simple MaterialX material binding
  - [ ] Complex node graphs
  - [ ] Multi-material scenes
  - [ ] MaterialX library usage examples

### 6. Documentation

#### 6.1 API Documentation
- [ ] **Header Documentation**
  - [ ] Document MaterialXConfigAPI usage
  - [ ] Document MaterialX shader types
  - [ ] Document conversion functions

- [ ] **Usage Examples**
  - [ ] Loading MaterialX files
  - [ ] Creating MaterialX materials programmatically
  - [ ] Converting MaterialX to render materials

#### 6.2 User Guide
- [ ] **MaterialX Integration Guide**
  - [ ] How to use MaterialX in USD files
  - [ ] Best practices for MaterialX materials
  - [ ] Performance considerations

## Implementation Priority

### Phase 1 (High Priority)
1. MaterialXConfigAPI parsing and reconstruction
2. OpenPBRSurface reconstruction
3. Basic NodeGraph support
4. MaterialX file reference resolution

### Phase 2 (Medium Priority)
1. Complete StandardSurface support
2. Enhanced node graph evaluation
3. Texture and image node support
4. Color space auto-conversion

### Phase 3 (Low Priority)
1. Geometry assignments and collections
2. Material variants
3. Unit system support
4. Advanced procedural nodes

## Code Locations

### Files to Modify

1. **`src/prim-reconstruct.cc`**
   - Add MaterialXConfigAPI reconstruction
   - Add OpenPBRSurface shader reconstruction
   - Add NodeGraph prim support

2. **`src/usdShade.hh`**
   - Extend MaterialXConfigAPI struct
   - Add NodeGraph struct
   - Complete shader definitions

3. **`src/usdMtlx.cc`**
   - Enhance MaterialX parsing
   - Add node graph support
   - Implement material conversion

4. **`src/tydra/render-data.cc`**
   - Add MaterialX shader conversion
   - Implement node evaluation
   - Handle texture references

5. **`src/composition.cc`**
   - Add MaterialX file reference support
   - Implement MaterialX composition rules

### New Files to Create

1. **`src/materialx-eval.hh/cc`**
   - Node graph evaluation engine
   - Connection resolver
   - Value computation

2. **`tests/test-materialx.cc`**
   - Comprehensive MaterialX tests
   - Validation suite

3. **`examples/materialx-viewer/`**
   - Example viewer for MaterialX materials
   - Demonstration of features

## Dependencies and Requirements

### External Dependencies
- None (maintain dependency-free approach)
- Optional: MaterialX validator for testing

### Build Configuration
- Add `TINYUSDZ_WITH_FULL_MATERIALX` option
- Enable by default when `TINYUSDZ_WITH_USDMTLX=ON`

## Performance Considerations

1. **Memory Management**
   - Cache parsed MaterialX documents
   - Lazy evaluation of node graphs
   - Efficient texture loading

2. **Optimization Opportunities**
   - Pre-compile node graphs to bytecode
   - SIMD color space conversions
   - Parallel node evaluation

## Compatibility Notes

1. **USD Compatibility**
   - Follow USD MaterialX schema conventions
   - Support Pixar's MaterialX integration patterns
   - Maintain compatibility with pxrUSD

2. **MaterialX Version Support**
   - Primary: MaterialX 1.38 (current)
   - Legacy: MaterialX 1.36, 1.37
   - Future: MaterialX 1.39+ preparation

## Validation Checklist

- [ ] All MaterialX example files load correctly
- [ ] Color spaces are properly converted
- [ ] Node graphs evaluate correctly
- [ ] Textures are loaded with correct parameters
- [ ] Round-trip preservation of MaterialX data
- [ ] Performance meets requirements
- [ ] Memory usage is bounded
- [ ] Security: no buffer overflows or memory leaks

## Related Documentation

- **[OpenPBR Parameters Reference](./openpbr-parameters-reference.md)** - Comprehensive parameter mapping guide
  - Complete list of all 38 OpenPBR parameters
  - Blender MaterialX export parameter names
  - Three.js MeshPhysicalMaterial support status
  - Conversion recommendations and limitations

## References

- [MaterialX Specification v1.38](https://www.materialx.org/docs/api/MaterialX_v1_38_Spec.pdf)
- [USD MaterialX Schema](https://openusd.org/release/api/usd_mtlx_page.html)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [MaterialX GitHub Repository](https://github.com/AcademySoftwareFoundation/MaterialX)
- [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709)
- [ITU-R BT.2020](https://www.itu.int/rec/R-REC-BT.2020)
- [ACES Documentation](https://www.oscars.org/science-technology/sci-tech-projects/aces)
- [sRGB Specification](https://www.w3.org/Graphics/Color/sRGB)

## Notes

- MaterialX support is critical for modern production pipelines
- Prioritize compatibility with major DCC tools (Maya, Houdini, Blender)
- Consider future integration with MaterialX code generation
- Maintain security-first approach in all implementations