# NodeMaterial Support via MaterialXLoader

This implementation adds support for routing TinyUSDZ-generated OpenPBR materials to Three.js `NodeMaterial` via `MaterialXLoader`, with a switchable option in the UI.

## Features

### 1. **Dual Material System**
- **MeshPhysicalMaterial** (default): Traditional Three.js material with manual parameter mapping
- **NodeMaterial**: Advanced node-based material system using MaterialX specification

### 2. **MaterialXLoader Integration**
- Converts OpenPBR data to MaterialX XML format
- Uses Three.js MaterialXLoader to parse and create NodeMaterial
- Supports both flat and grouped parameter formats from TinyUSDZ

### 3. **UI Toggle**
New "Material Rendering" panel in the GUI with:
- **Use NodeMaterial (MaterialX)**: Toggle checkbox
- **Reload Materials**: Button to refresh materials with new settings

## How It Works

### Material Creation Flow

```
TinyUSDZ OpenPBR Data
         ↓
    [Toggle Check]
         ↓
   ┌─────────┴─────────┐
   │                   │
   ▼                   ▼
NodeMaterial    MeshPhysicalMaterial
(MaterialX)       (Manual Mapping)
```

### NodeMaterial Path

1. **OpenPBR → MaterialX XML**: `convertOpenPBRToMaterialXML()` function converts OpenPBR data to MaterialX 1.39 XML format
2. **MaterialX → NodeMaterial**: Three.js MaterialXLoader parses the XML and creates NodeMaterial with shader graph
3. **Rendering**: NodeMaterial uses WebGPU-ready node-based shading system

### MeshPhysicalMaterial Path (Default)

1. **Direct Mapping**: OpenPBR parameters are manually mapped to MeshPhysicalMaterial properties
2. **Fallback**: Used when NodeMaterial creation fails or is disabled
3. **Compatibility**: Works with standard WebGL rendering

## Files Modified

- **materialx.js**:
  - Added `MaterialXLoader` import
  - Added `useNodeMaterial` and `materialXLoader` global variables
  - Modified `createOpenPBRMaterial()` to support both material types
  - Added "Material Rendering" GUI panel

- **convert-openpbr-to-mtlx.js** (new):
  - Utility function to convert OpenPBR data to MaterialX XML
  - Supports both flat (`base_color`) and grouped (`base.base_color`) formats
  - Generates MaterialX 1.39 compatible XML

## Usage

### Via UI

1. Load a USD file with OpenPBR materials
2. Open the **Material Rendering** panel in the GUI
3. Toggle **Use NodeMaterial (MaterialX)** checkbox
4. Materials will automatically reload with the new setting

### Programmatically

```javascript
// Enable NodeMaterial mode
useNodeMaterial = true;

// Reload materials
await loadMaterials();
```

## MaterialX XML Structure

Generated MaterialX XML follows this structure:

```xml
<?xml version="1.0"?>
<materialx version="1.39">
  <open_pbr_surface name="Material_shader" type="surfaceshader">
    <input name="base_color" type="color3" value="0.9, 0.7, 0.3" />
    <input name="base_metalness" type="float" value="0.85" />
    <input name="base_roughness" type="float" value="0.5" />
    <!-- ... all OpenPBR parameters ... -->
  </open_pbr_surface>

  <surfacematerial name="Material" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="Material_shader" />
  </surfacematerial>
</materialx>
```

## Parameter Mapping

### Supported OpenPBR Parameters

| Category | Parameters |
|----------|-----------|
| **Base** | base_weight, base_color, base_roughness, base_metalness |
| **Specular** | specular_weight, specular_color, specular_roughness, specular_ior, specular_ior_level, specular_anisotropy, specular_rotation |
| **Transmission** | transmission_weight, transmission_color, transmission_depth, transmission_scatter, transmission_scatter_anisotropy, transmission_dispersion |
| **Subsurface** | subsurface_weight, subsurface_color, subsurface_radius, subsurface_scale, subsurface_anisotropy |
| **Sheen** | sheen_weight, sheen_color, sheen_roughness |
| **Coat** | coat_weight, coat_color, coat_roughness, coat_anisotropy, coat_rotation, coat_ior, coat_affect_color, coat_affect_roughness |
| **Emission** | emission_luminance, emission_color |
| **Geometry** | opacity, geometry_normal, geometry_tangent |

## Benefits of NodeMaterial

1. **Shader Graph**: Visual node-based material system
2. **WebGPU Ready**: Optimized for modern graphics APIs
3. **Standard Compliance**: Uses MaterialX industry standard
4. **Extensibility**: Easy to extend with custom nodes
5. **Performance**: Can be more efficient than traditional materials

## Fallback Behavior

If NodeMaterial creation fails:
1. Error is logged to console
2. Automatically falls back to MeshPhysicalMaterial
3. User is notified via console warning
4. Scene continues to render normally

## Testing

Test the implementation:
1. Open `materialx.html` in browser
2. Load `models/openpbr-glass-sphere.usda` or similar
3. Toggle between material types in the GUI
4. Verify materials render correctly in both modes
5. Check console for any errors

## Browser Requirements

- Modern browser with ES modules support
- WebGL 2.0 or WebGPU support
- Three.js r161 or later

## Known Limitations

1. Texture mapping not yet implemented in NodeMaterial path
2. Some advanced OpenPBR features may not have direct MaterialX equivalents
3. MaterialXLoader requires specific MaterialX XML structure

## Future Enhancements

- [ ] Add texture support in MaterialX XML generation
- [ ] Implement normal map and displacement support
- [ ] Add color space conversions in MaterialX
- [ ] Support custom shader nodes
- [ ] Export NodeMaterial back to MaterialX
