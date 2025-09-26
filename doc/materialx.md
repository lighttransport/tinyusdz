# MaterialX Support in TinyUSDZ

This document describes the MaterialX integration and color space support in TinyUSDZ.

## Overview

TinyUSDZ provides comprehensive support for MaterialX, including a full suite of color space conversions required for proper MaterialX document processing. The library can parse MaterialX (.mtlx) files and handle all standard MaterialX color spaces.

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

## References

- [MaterialX Specification](https://www.materialx.org/)
- [ITU-R BT.709](https://www.itu.int/rec/R-REC-BT.709)
- [ITU-R BT.2020](https://www.itu.int/rec/R-REC-BT.2020)
- [ACES Documentation](https://www.oscars.org/science-technology/sci-tech-projects/aces)
- [sRGB Specification](https://www.w3.org/Graphics/Color/sRGB)