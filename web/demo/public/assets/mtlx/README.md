# MaterialX Example Files

This directory contains MaterialX (.mtlx) example files for testing and demonstration purposes.

## File Organization

### Standard Surface Materials (from MaterialX GitHub)
Downloaded from: https://github.com/AcademySoftwareFoundation/MaterialX/tree/main/resources/Materials/Examples/StandardSurface

- `standard_surface_brass_tiled.mtlx` - Tiled brass material with textures
- `standard_surface_brick_procedural.mtlx` - Procedural brick material
- `standard_surface_chess_set.mtlx` - Complex chess set scene with multiple materials
- `standard_surface_copper.mtlx` - Simple copper metal material
- `standard_surface_glass.mtlx` - Transparent glass material
- `standard_surface_gold.mtlx` - Gold metal material
- `standard_surface_greysphere_calibration.mtlx` - Calibration sphere
- `standard_surface_look_brass_tiled.mtlx` - Brass material with look definition
- `standard_surface_marble_solid.mtlx` - Solid marble procedural material
- `standard_surface_velvet.mtlx` - Velvet fabric material
- `standard_surface_wood_tiled.mtlx` - Tiled wood material with textures

### OpenPBR Surface Materials (custom examples)
Created specifically for TinyUSDZ testing:

- `open_pbr_red_metal.mtlx` - Red metallic material
- `open_pbr_blue_glass.mtlx` - Blue transparent glass
- `open_pbr_gold.mtlx` - Gold metal with specular color
- `open_pbr_plastic.mtlx` - Green plastic with clearcoat

## Usage

### In Three.js Demo
1. Open `web/js/materialx.html` in a browser
2. Click "📥 Import MTLX" button
3. Select one of these .mtlx files
4. The material will be applied to the selected object

### In C++ Code
```cpp
#include "usdMtlx.hh"

tinyusdz::AssetResolutionResolver resolver;
tinyusdz::MtlxModel mtlx;
std::string warn, err;

bool success = tinyusdz::ReadMaterialXFromFile(
    resolver,
    "web/demo/public/assets/mtlx/open_pbr_gold.mtlx",
    &mtlx,
    &warn,
    &err
);

if (success) {
    tinyusdz::PrimSpec ps;
    tinyusdz::ToPrimSpec(mtlx, ps, &err);
}
```

## Material Complexity

### Simple Materials (good for initial testing)
- `standard_surface_copper.mtlx` (814 bytes)
- `standard_surface_gold.mtlx` (683 bytes)
- `standard_surface_velvet.mtlx` (862 bytes)
- All `open_pbr_*.mtlx` files (~500-800 bytes each)

### Medium Complexity
- `standard_surface_brass_tiled.mtlx` (1.5 KB) - includes textures
- `standard_surface_glass.mtlx` (1.2 KB) - transmission
- `standard_surface_wood_tiled.mtlx` (1.5 KB) - tiled textures

### Complex Materials
- `standard_surface_marble_solid.mtlx` (3.5 KB) - procedural generation
- `standard_surface_brick_procedural.mtlx` (7.3 KB) - complex node graph
- `standard_surface_chess_set.mtlx` (32 KB) - multiple materials, complete scene

## Downloading Textures

Many .mtlx files reference external texture images. To keep the git repository lean, textures are **not included** and must be downloaded on-demand.

### Quick Start

```bash
# Download minimal set (5 files, ~10 MB)
./download_textures.sh --minimal

# Download all textures (~50+ files, ~50 MB)
./download_textures.sh

# Remove downloaded textures
./download_textures.sh --clean
```

### Script Details

**`download_textures.sh`** - Automated texture downloader
- Downloads from MaterialX GitHub repository
- Creates `images/` directory (gitignored)
- Colored output with progress indicators
- Handles subdirectories (e.g., `chess_set/`)

**Options:**
- `--minimal` - Downloads 5 essential textures:
  - `brass_color.jpg`, `brass_roughness.jpg`
  - `wood_color.jpg`, `wood_roughness.jpg`
  - `greysphere_calibration.png`
- (no args) - Downloads all ~50+ texture files
- `--clean` - Removes `images/` directory
- `--help` - Shows usage information

**Requirements:**
- `curl` command-line tool
- Internet connection

**Texture Coverage:**
| Material | Texture Files | Size |
|----------|--------------|------|
| Brass | 2 files | ~7 MB |
| Brick | 6 files | ~15 MB |
| Wood | 2 files | ~5 MB |
| Chess Set | 43 files | ~25 MB |
| Calibration | 1 file | ~1 MB |

### Manual Download

If you prefer manual download, textures are available at:
https://github.com/AcademySoftwareFoundation/MaterialX/tree/main/resources/Images

Place downloaded files in the `images/` directory.

## Notes

### Texture References
Some files reference external textures (e.g., `brass_color.jpg`, `wood_diff.jpg`). Use the `download_textures.sh` script to fetch them automatically. The MaterialX files will load, but textures will be missing until downloaded.

### Node Graph Support
Files with complex node graphs (e.g., `standard_surface_brick_procedural.mtlx`) may not fully render in TinyUSDZ as node graph support is currently limited to surface shaders.

### MaterialX Versions
All files use MaterialX 1.38 or 1.39 format. TinyUSDZ supports MaterialX 1.36, 1.37, and 1.38.

## Testing Recommendations

**For OpenPBR Testing:**
1. Start with `open_pbr_gold.mtlx` (simple metallic)
2. Try `open_pbr_plastic.mtlx` (dielectric with coat)
3. Test `open_pbr_blue_glass.mtlx` (transmission)

**For Standard Surface Testing:**
1. Start with `standard_surface_gold.mtlx` (simple)
2. Try `standard_surface_brass_tiled.mtlx` (with textures)
3. Test `standard_surface_glass.mtlx` (transmission)

**For Advanced Testing:**
1. `standard_surface_marble_solid.mtlx` (procedural)
2. `standard_surface_brick_procedural.mtlx` (complex node graph)
3. `standard_surface_chess_set.mtlx` (scene with multiple materials)

## Source

- **Standard Surface Examples**: MaterialX Official Repository (Apache 2.0 License)
  - URL: https://github.com/AcademySoftwareFoundation/MaterialX
  - Path: resources/Materials/Examples/StandardSurface/
  - Downloaded: January 2025

- **OpenPBR Examples**: Created for TinyUSDZ (Apache 2.0 License)
  - Based on OpenPBR specification
  - Created: January 2025

## See Also

- [MaterialX Specification](https://materialx.org/)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [TinyUSDZ MaterialX Support Status](../../../../../MATERIALX-SUPPORT-STATUS.md)
- [C++ MaterialX Import Guide](../../../../../C++_MATERIALX_IMPORT.md)
