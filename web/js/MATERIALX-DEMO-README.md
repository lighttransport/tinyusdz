# TinyUSDZ MaterialX/OpenPBR Three.js Demo

A standalone web application that demonstrates loading USD files with OpenPBR materials and rendering them using Three.js with interactive parameter editing.

## Features

- **USD File Loading**: Load USD/USDA/USDC/USDZ files via TinyUSDZ WebAssembly
- **OpenPBR Material Support**: Full support for OpenPBR material parameters
- **Interactive Parameter Editing**: Real-time material parameter adjustment with sliders and color pickers
- **Material Export**: Export edited materials to JSON and MaterialX XML (.mtlx) formats
- **Texture Preview & Control**: View, toggle, and manage texture maps for all material parameters
- **Display-P3 Color Space Support**: Automatic detection and optional use of Display-P3 wide color gamut
- **Synthetic HDR Environments**: Two built-in HDR environments (studio lighting and all-white)
- **Object Selection**: Click on objects to select and edit their materials
- **Material Panel**: View and select all materials in the loaded USD file

## Setup

1. **Build TinyUSDZ WASM Module**:
   ```bash
   cd web
   ./bootstrap-linux-wasm64.sh  # or use appropriate script for your platform
   cd build
   make -j8
   ```

2. **Serve the Demo**:
   ```bash
   cd web
   python -m http.server 8000  # or any other static file server
   ```

3. **Open in Browser**:
   Navigate to `http://localhost:8000/js/materialx.html`

## Usage

### Loading USD Files

1. **Load Custom File**: Click "Load USD File" button to browse and select a USD file from your computer
2. **Load Sample**: Click "Load Sample" button to load a built-in sample USD file
3. **Drag & Drop**: (Future enhancement) Drag USD files directly onto the viewport

### Interacting with Materials

1. **Select Object**: Click on any mesh in the scene to select it
2. **Material Panel**: View all materials in the bottom-left panel
3. **Edit Parameters**: Use the GUI controls on the right to adjust:
   - Base color, metalness, roughness
   - Specular properties (IOR, anisotropy)
   - Transmission (glass-like effects)
   - Subsurface scattering
   - Coat (clearcoat) properties
   - Thin film interference
   - Emission (self-illumination)
   - Geometry properties (opacity, thin-walled)

### Material Import & Export

**Import MaterialX Files:**
- Click "📥 Import MTLX" button to load a MaterialX XML file
- Select an object in the scene first
- The imported material will be applied to the selected object
- Supports MaterialX 1.38 format with OpenPBR surface shader
- All OpenPBR parameters are parsed and applied automatically

**Export Materials:**

Export your edited materials in industry-standard formats:

**Export Formats:**
- **JSON Format**: Complete material data including all parameters, texture references, and color space settings
- **MaterialX XML (.mtlx)**: Standard MaterialX 1.38 format with OpenPBR surface shader

**How to Export:**
1. Select a material from the material panel
2. Edit parameters as desired (colors, roughness, metalness, etc.)
3. Click "📄 Export JSON" or "📄 Export MaterialX XML" in the material panel
4. Or use the export controls in the dat.GUI panel under "Export Material"

**JSON Export Includes:**
- Material name and metadata
- All OpenPBR parameters (base, specular, transmission, coat, emission, etc.)
- Texture references with IDs and enabled state
- Color space settings for each texture
- Export timestamp

**MaterialX XML Export Includes:**
- Proper MaterialX 1.38 document structure
- `open_pbr_surface` shader node with all parameters
- Texture image nodes with color space information
- Channel extraction for data textures (roughness, metalness)
- Compatible with MaterialX viewers and renderers

### Environment Controls

- **Toggle HDR**: Switch between studio lighting and all-white environment
- **Exposure**: Adjust the overall brightness of the scene
- **Background**: Change the background color of the viewport

### Texture Controls

The demo provides comprehensive texture management for materials:

- **Automatic Loading**: Textures are automatically loaded from USD files
- **Texture Panel**: View all textures for the selected material (bottom-right)
- **Thumbnail Previews**: See scaled-down previews of each texture map
- **Toggle On/Off**: Enable or disable individual texture maps in real-time
- **Full-Size View**: Click on any thumbnail to view the full-resolution texture
- **Supported Maps**:
  - Base Color (Albedo/Diffuse)
  - Normal Map
  - Roughness Map
  - Metalness Map
  - Emission Map
  - Ambient Occlusion
  - Bump Map
  - Displacement Map
  - Alpha/Opacity Map

**Texture Information Display:**
- Texture dimensions (width x height)
- Texture ID from USD file
- Current enabled/disabled status
- Real-time preview updates

**Interactive Features:**
- Click texture thumbnail to enlarge
- Toggle textures ON/OFF to see material with/without
- **Color Space Selection**: Choose the input color space for each texture
  - sRGB (standard for color textures)
  - Linear (Raw) - for data textures
  - Rec.709 (broadcast standard)
  - ACES2065-1 (ACES AP0 working space)
  - ACEScg (ACES AP1 CG working space)
- **Texture Transform Controls**: Adjust texture mapping in real-time
  - Offset X/Y: Move texture position (-2 to +2)
  - Scale X/Y: Tile or stretch texture (0.1 to 10x)
  - Rotation: Rotate texture (0° to 360°)
  - Reset button to restore default transform
- Textures are cached for performance
- Proper wrap mode handling (Repeat/Clamp/Mirror)
- Automatic mipmap generation for better quality

**External Texture Loading:**
- Click "🖼️ Load Texture" to load external texture files
- Supports HDR (.hdr) and EXR (.exr) high dynamic range textures
- Also supports standard formats (PNG, JPG, JPEG)
- HDR/EXR textures are loaded using Three.js loaders
- Automatically applied to selected object's base color map
- Proper color space handling for HDR content

### Color Space Controls

The demo automatically detects Display-P3 support and provides options to switch between color spaces:

- **Automatic Detection**: The demo checks for Display-P3 support on load
- **Color Space Switching**: Toggle between sRGB and Display-P3 color spaces
- **Wider Gamut**: Display-P3 provides ~25% more color range than sRGB
- **Visual Feedback**: UI shows current color space status and availability
- **Button Toggle**: Quick toggle button appears when Display-P3 is available

**Benefits of Display-P3:**
- More vibrant and saturated colors
- Better representation of OpenPBR material colors
- Improved color accuracy for professional content
- Smoother color gradients in HDR environments

**Requirements for Display-P3:**
- Display with P3 color gamut support (modern MacBooks, iMacs, high-end monitors)
- Browser with Display-P3 WebGL support (Chrome 104+, Safari 15+, Firefox 113+)
- WebGL2 context with drawingBufferColorSpace support

## OpenPBR Parameters

The demo supports the full OpenPBR specification with the following parameter groups:

### Base Layer
- `weight`: Overall weight of the base layer
- `color`: Base color (albedo)
- `metalness`: Metallic property (0=dielectric, 1=metal)
- `diffuse_roughness`: Roughness of diffuse reflection

### Specular Layer
- `weight`: Specular contribution weight
- `color`: Specular tint color
- `roughness`: Specular roughness (0=mirror, 1=rough)
- `ior`: Index of refraction
- `anisotropy`: Anisotropic reflection amount
- `rotation`: Anisotropic rotation angle

### Transmission
- `weight`: Transmission amount (transparency)
- `color`: Transmission filter color
- `depth`: Transmission depth for volumetric absorption
- `scatter`: Volume scattering color
- `scatter_anisotropy`: Scattering direction bias
- `dispersion`: Chromatic dispersion amount

### Subsurface
- `weight`: Subsurface scattering contribution
- `color`: Subsurface color
- `radius`: Scattering radius (RGB channels)
- `scale`: Overall subsurface scale
- `anisotropy`: Subsurface scattering directionality

### Coat Layer
- `weight`: Clearcoat layer weight
- `color`: Coat tint color
- `roughness`: Coat roughness
- `anisotropy`: Coat anisotropic amount
- `rotation`: Coat anisotropic rotation
- `ior`: Coat index of refraction
- `affect_color`: How much coat affects base color
- `affect_roughness`: How much coat affects base roughness

### Thin Film
- `thickness`: Thin film thickness in nanometers
- `ior`: Thin film index of refraction

### Emission
- `weight`: Emission contribution
- `color`: Emission color
- `intensity`: Emission intensity multiplier

### Geometry
- `opacity`: Overall opacity
- `thin_walled`: Whether geometry is thin-walled
- `normal`: Normal map (when textures are supported)
- `tangent`: Tangent map for anisotropy

## Recent Enhancements (2025-01)

### MaterialX Import System
The demo now supports importing MaterialX XML files:
- **XML Parsing**: Uses browser DOMParser to parse MaterialX 1.38 XML
- **Parameter Extraction**: Automatically extracts all OpenPBR parameters from `<open_pbr_surface>` node
- **Texture References**: Parses `<image>` nodes with color space and channel information
- **Material Application**: Applies imported materials to selected objects in the scene
- **Error Handling**: Comprehensive validation and user-friendly error messages

### Texture Transform System
Real-time texture mapping controls for all texture maps:
- **Offset Controls**: X/Y translation with range -2 to +2
- **Scale Controls**: X/Y tiling from 0.1x to 10x
- **Rotation Control**: 0° to 360° rotation
- **Live Preview**: Changes update in real-time on the 3D model
- **Reset Function**: One-click restore to default transform
- **UI Integration**: Embedded in texture panel for easy access

### HDR/EXR Texture Loading
Support for high dynamic range textures via Three.js loaders:
- **Format Support**: HDR (RGBE), EXR (OpenEXR), plus PNG/JPG
- **Three.js Integration**: Uses RGBELoader and EXRLoader
- **Color Space Handling**: Proper linear color space for HDR content
- **Environment Mapping**: Automatic equirectangular reflection mapping
- **File Browser**: Simple file selection dialog

### Enhanced Error Handling
Robust validation and error reporting throughout:
- **Texture ID Validation**: Checks for valid, non-negative integer IDs
- **Material Index Validation**: Validates indices against bounds
- **Dimension Validation**: Ensures textures have valid dimensions
- **Channel Validation**: Verifies 1-4 channel images
- **User-Friendly Messages**: Simplified error messages for common issues
- **Fallback Materials**: Creates default materials when loading fails
- **Context-Aware Logging**: Detailed console logs with context information

## Technical Details

### Material Export System

The demo implements comprehensive material export functionality:

1. **JSON Export Format**:
   ```json
   {
     "materialName": "Material_0",
     "exportDate": "2025-01-02T12:00:00.000Z",
     "version": "1.0",
     "hasOpenPBR": true,
     "openPBR": {
       "base": {
         "color": [0.8, 0.8, 0.8],
         "metalness": 0.5,
         "weight": 1.0
       },
       "specular": {
         "roughness": 0.3,
         "ior": 1.5
       }
     },
     "textures": {
       "map": {
         "textureId": 0,
         "enabled": true,
         "colorSpace": "srgb"
       }
     }
   }
   ```

2. **MaterialX XML Export**:
   - Compliant with MaterialX 1.38 specification
   - Uses `open_pbr_surface` shader node
   - Proper texture node structure with color space
   - Channel extraction for scalar textures
   - Example structure:
   ```xml
   <materialx version="1.38">
     <surfacematerial name="MyMaterial" type="material">
       <input name="surfaceshader" type="surfaceshader" nodename="MyMaterial_shader" />
     </surfacematerial>

     <open_pbr_surface name="MyMaterial_shader" type="surfaceshader">
       <input name="base_color" type="color3" value="0.8, 0.8, 0.8" />
       <input name="base_metalness" type="float" value="0.5" />
       <input name="specular_roughness" type="float" value="0.3" />
     </open_pbr_surface>
   </materialx>
   ```

3. **Export Features**:
   - Captures all edited parameter values from UI
   - Preserves texture on/off state
   - Includes color space settings per texture
   - Exports only enabled textures in MaterialX
   - Automatic filename based on material name
   - Browser-based file download (no server required)

### Texture Loading Pipeline

The demo implements a complete texture loading and management system:

1. **USD Integration**:
   - Reads texture IDs from OpenPBR material parameters
   - Fetches texture metadata via `getTexture(textureId)`
   - Loads image data via `getImage(textureImageId)`

2. **Image Processing**:
   - Supports 1, 2, 3, and 4 channel images (Grayscale, GA, RGB, RGBA)
   - Converts raw pixel data to Canvas ImageData
   - Handles different color spaces (sRGB, Linear)
   - Generates mipmaps for optimal quality

3. **Texture Configuration**:
   - Applies USD wrap modes (Repeat, Clamp, Mirror)
   - Sets appropriate filtering (Linear/Mipmap)
   - Enables anisotropic filtering for better quality
   - Configures color space based on image metadata

4. **Caching & Performance**:
   - Textures are cached globally by ID
   - Prevents duplicate loading of the same texture
   - Proper disposal when clearing scenes
   - Memory-efficient thumbnail generation

5. **Three.js Mapping**:
   ```javascript
   OpenPBR base.color → Three.js map (Base Color)
   OpenPBR base.metalness → Three.js metalnessMap
   OpenPBR specular.roughness → Three.js roughnessMap
   OpenPBR emission.color → Three.js emissiveMap
   OpenPBR geometry.normal → Three.js normalMap
   ```

### Texture Color Space Conversion

The demo implements advanced color space management for texture inputs:

1. **Supported Color Spaces**:
   - **sRGB**: Standard color space for images (gamma 2.2)
   - **Linear (Raw)**: No conversion, for data textures
   - **Rec.709**: Broadcast standard (same gamma as sRGB)
   - **ACES2065-1**: Academy Color Encoding System AP0
   - **ACEScg**: ACES CG working space (AP1)

2. **Shader-Based Conversion**:
   - Uses custom GLSL shaders via `onBeforeCompile`
   - Converts textures to linear space in the fragment shader
   - Supports per-texture color space selection
   - Matrix transformations for ACES color spaces

3. **Implementation Details**:
   ```javascript
   // Color space conversions in GLSL
   - sRGB to Linear: Inverse gamma 2.4 with linear segment
   - Rec.709 to Linear: Same as sRGB conversion
   - ACES AP0 to Linear: 3x3 matrix transformation
   - ACEScg to Linear: 3x3 matrix transformation
   ```

4. **Smart Defaults**:
   - Color textures (base, emissive): sRGB
   - Data textures (roughness, metalness, normal): Linear
   - User can override per texture

5. **Shader Injection**:
   - Custom uniforms for each texture's color space
   - Modified fragment shader chunks
   - Real-time color space switching without reload

### Color Space Implementation

The demo implements proper color space handling for accurate material rendering:

1. **Detection**: Uses `window.matchMedia('(color-gamut: p3)')` to detect display capabilities
2. **WebGL Configuration**: Sets `drawingBufferColorSpace` on the WebGL2 context
3. **Three.js Integration**: Uses `THREE.DisplayP3ColorSpace` for renderer output
4. **Color Conversion**: Simplified sRGB to Display-P3 conversion with saturation enhancement
5. **Material Updates**: All material colors are converted based on active color space

### Three.js Material Mapping

OpenPBR parameters are mapped to Three.js `MeshPhysicalMaterial` properties:

- OpenPBR `base.color` → Three.js `color`
- OpenPBR `base.metalness` → Three.js `metalness`
- OpenPBR `specular.roughness` → Three.js `roughness`
- OpenPBR `specular.ior` → Three.js `ior`
- OpenPBR `transmission.weight` → Three.js `transmission`
- OpenPBR `coat.weight` → Three.js `clearcoat`
- OpenPBR `coat.roughness` → Three.js `clearcoatRoughness`
- OpenPBR `emission.color` → Three.js `emissive`
- OpenPBR `emission.intensity` → Three.js `emissiveIntensity`

### Synthetic HDR Generation

The demo creates procedural HDR environment maps:

1. **Studio Lighting**: Gradient-based environment with key and fill light spots
2. **All White**: Uniform white environment for neutral lighting

These are generated on-the-fly using Canvas 2D API and converted to equirectangular environment maps.

### USD Material Formats

The demo can handle materials in multiple formats:

1. **JSON Format**: Complete OpenPBR parameter data
2. **XML Format**: MaterialX 1.38 compliant XML
3. **Legacy Format**: Backward compatibility with older TinyUSDZ versions

## Browser Requirements

- Modern browser with WebAssembly support
- WebGL 2.0 support recommended
- Sufficient memory for large USD files (memory limits can be configured)

## Limitations

- ~~Texture maps are not yet supported~~ ✓ **Now supported!**
- ~~Texture transforms (scale, rotation, translation) not yet implemented~~ ✓ **Now supported!**
- ~~HDR/EXR textures require additional processing~~ ✓ **Now supported via Three.js loaders!**
- ~~Import MaterialX XML files (currently export-only)~~ ✓ **Now supported!**
- Some advanced OpenPBR features may not have direct Three.js equivalents
- Large USD files with many textures may take time to load
- Animation and time-based properties are not supported in this demo
- Procedural textures are not supported (only image-based textures)
- MaterialX import doesn't automatically load referenced texture files (texture references are parsed but not loaded)

## Future Enhancements

- [x] Texture map support for all OpenPBR channels ✓ **Done!**
- [x] Texture preview and toggle controls ✓ **Done!**
- [x] Texture color space conversion (sRGB, Rec.709, ACES) ✓ **Done!**
- [x] Export MaterialX XML and JSON ✓ **Done!**
- [x] Import MaterialX XML files ✓ **Done!**
- [x] Texture transform controls (scale, offset, rotation) ✓ **Done!**
- [x] HDR/EXR texture loading and display ✓ **Done!**
- [ ] Automatic texture file loading from MaterialX imports
- [ ] Additional color spaces (DCI-P3, Adobe RGB)
- [ ] Animation timeline support
- [ ] Multiple viewport layouts
- [ ] Save edited materials back to USD format
- [ ] Custom HDR image loading for IBL
- [ ] Path-traced rendering mode
- [ ] Node-based material editor
- [ ] Texture coordinate channel selection (UV0, UV1, etc.)
- [ ] Texture compression options

## Troubleshooting

1. **WASM Module Not Loading**: Ensure the TinyUSDZ WASM files are built and accessible at `../dist/`
2. **USD File Parse Errors**: Check console for detailed error messages
3. **Performance Issues**: Try smaller USD files or reduce polygon count
4. **Material Not Updating**: Ensure the USD file contains OpenPBR material data
5. **Textures Not Showing**:
   - Check browser console for texture loading errors
   - Verify USD file contains embedded or referenced texture data
   - Ensure textures are toggled ON in the texture panel
   - Check that texture IDs are valid (>= 0)
6. **Texture Quality Issues**:
   - Enable anisotropic filtering (automatic in this demo)
   - Check that mipmaps are generated (automatic)
   - Verify texture resolution is appropriate for the model
7. **Memory Issues with Large Textures**:
   - Monitor browser memory usage
   - Consider downsampling very large textures
   - Clear scene before loading new files
8. **Color Space Issues**:
   - If colors look wrong, try different color space settings
   - Color textures usually need sRGB
   - Data textures (roughness, metalness) should use Linear
   - ACES textures require proper tagging in the source USD
   - Check browser console for shader compilation errors
9. **Export Issues**:
   - Ensure a material is selected before attempting export
   - Exported MaterialX files reference textures by ID (e.g., `texture_0.png`)
   - Texture files are not embedded in export (only references)
   - JSON export preserves all UI state including disabled textures
   - MaterialX export only includes enabled textures
   - Check browser downloads folder for exported files

## License

This demo is part of the TinyUSDZ project. See the main project LICENSE file for details.