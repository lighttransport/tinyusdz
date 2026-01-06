# Material JSON Viewer

A comprehensive JSON viewer for inspecting Tydra-converted MaterialX and UsdPreviewSurface material data in the TinyUSDZ web demo.

## Overview

The Material JSON Viewer provides a syntax-highlighted, tabbed interface to inspect material data at various stages of the conversion pipeline, from raw USD data to Three.js material properties.

## Features

- **Multi-Tab View**: Switch between different material representations
- **Syntax Highlighting**: Color-coded JSON for easy reading
- **Copy to Clipboard**: One-click copying of JSON data
- **Export**: Download material data as JSON files
- **Automatic Detection**: Shows available data based on material type

## Usage

### Opening the JSON Viewer

1. Load a USD file with materials
2. Select an object or material from the Materials panel
3. Click the **📋 Material JSON** button in the top toolbar
4. The JSON viewer will open with the material data

### Viewing Different Data Types

The viewer has 4 tabs showing different aspects of the material:

#### 1. OpenPBR Surface Tab
Shows the OpenPBR Surface material definition with all layers:

```json
{
  "name": "MaterialName",
  "type": "OpenPBR Surface",
  "hasOpenPBR": true,
  "openPBR": {
    "base": {
      "color": [0.8, 0.8, 0.8],
      "weight": 1.0,
      "metalness": 0.0,
      "diffuse_roughness": 0.0
    },
    "specular": {
      "weight": 1.0,
      "color": [1.0, 1.0, 1.0],
      "roughness": 0.3,
      "ior": 1.5,
      "anisotropy": 0.0
    },
    "transmission": { ... },
    "coat": { ... },
    "emission": { ... },
    "geometry": { ... }
  }
}
```

**Available when**: Material has OpenPBR data (`hasOpenPBR: true`)

**Use case**: Understanding the complete OpenPBR material definition, debugging MaterialX export, comparing with Blender exports

#### 2. UsdPreviewSurface Tab
Shows the UsdPreviewSurface material definition:

```json
{
  "name": "MaterialName",
  "type": "UsdPreviewSurface",
  "hasUsdPreviewSurface": true,
  "usdPreviewSurface": {
    "diffuseColor": [0.18, 0.18, 0.18],
    "roughness": 0.5,
    "metallic": 0.0,
    "specularColor": [1.0, 1.0, 1.0],
    "ior": 1.5,
    "clearcoat": 0.0,
    "clearcoatRoughness": 0.01,
    "emissiveColor": [0.0, 0.0, 0.0],
    "opacity": 1.0,
    "normal": [0.0, 0.0, 1.0]
  }
}
```

**Available when**: Material has UsdPreviewSurface data (`hasUsdPreviewSurface: true`)

**Use case**: Inspecting USD standard material format, understanding fallback materials

#### 3. Raw Material Data Tab
Shows the complete raw material data as received from TinyUSDZ:

```json
{
  "name": "MaterialName",
  "hasOpenPBR": true,
  "hasUsdPreviewSurface": false,
  "openPBR": { ... },
  // Additional metadata, texture IDs, etc.
}
```

**Available when**: Always (if material selected)

**Use case**:
- Debugging material loading issues
- Seeing texture IDs and references
- Understanding the complete Tydra conversion output
- Finding additional metadata not shown in other tabs

#### 4. Three.js Material Tab
Shows the Three.js MeshPhysicalMaterial properties:

```json
{
  "type": "MeshPhysicalMaterial",
  "name": "MaterialName",
  "uuid": "...",
  "color": { "r": 0.8, "g": 0.8, "b": 0.8 },
  "metalness": 0.0,
  "roughness": 0.3,
  "ior": 1.5,
  "transmission": 0.0,
  "clearcoat": 0.0,
  "emissive": { "r": 0.0, "g": 0.0, "b": 0.0 },
  "textures": {
    "map": "Texture(123)",
    "normalMap": "Texture(124)",
    "roughnessMap": "Texture(125)"
  },
  "envMapIntensity": 1.0
}
```

**Available when**: Material has been converted to Three.js

**Use case**:
- Understanding how MaterialX/USD converts to Three.js
- Debugging rendering issues
- Verifying texture assignments
- Checking final material state used for rendering

### Syntax Highlighting

JSON is color-coded for readability:
- **Blue** (`#79B8FF`): Property keys
- **Light Blue** (`#9ECBFF`): String values
- **Red** (`#F97583`): Number values
- **Orange** (`#FFAB70`): Boolean values (true/false)
- **Purple** (`#B392F0`): null values

### Actions

**Copy to Clipboard**
- Copies the current tab's JSON to clipboard
- Button shows "✓ Copied!" confirmation
- Use for pasting into documentation, bug reports, or analysis tools

**Download JSON**
- Downloads current tab as a `.json` file
- Filename format: `{materialname}_{tabtype}.json`
- Examples:
  - `MetallicMaterial_openpbr.json`
  - `GlassMaterial_usdpreview.json`
  - `WoodMaterial_raw.json`
  - `PlasticMaterial_threejs.json`

**Close**
- Closes the JSON viewer
- Can also press ESC (if implemented)

## Use Cases

### 1. Debugging Material Conversion

When a material doesn't look right in the viewer:

1. Open JSON viewer
2. Check **Raw Material Data** tab to see what TinyUSDZ loaded
3. Check **OpenPBR/UsdPreviewSurface** tab to see converted data
4. Check **Three.js Material** tab to see final rendering material
5. Compare values to identify conversion issues

### 2. MaterialX Export Verification

When exporting materials to MaterialX:

1. Select material
2. Open JSON viewer → **OpenPBR** tab
3. Export material as MaterialX using "Export MaterialX (.mtlx)" button
4. Compare JSON values with exported MaterialX XML
5. Verify parameter mapping is correct

### 3. Blender Comparison

When comparing Blender's MaterialX export with TinyUSDZ:

1. Export from Blender with MaterialX enabled
2. Load in TinyUSDZ demo
3. Open JSON viewer → **OpenPBR** tab
4. Compare with Blender's Principled BSDF → OpenPBR mapping (see [materialx.md](../../../doc/materialx.md))
5. Verify parameter names and values match

### 4. Documentation & Bug Reports

When reporting issues or documenting materials:

1. Select problematic material
2. Open JSON viewer
3. Click "Copy to Clipboard" or "Download JSON"
4. Paste into GitHub issue, documentation, or email
5. Provides complete material context for debugging

### 5. Learning MaterialX

When learning about MaterialX structure:

1. Load various sample materials
2. Open JSON viewer
3. Switch between tabs to see different representations
4. Understand how USD → MaterialX → Three.js conversion works
5. See relationship between different material systems

## Technical Details

### Implementation

The JSON viewer is implemented in `material-json-viewer.js`:

**Key Functions:**
- `showMaterialJSON(material)` - Display material data
- `extractOpenPBRData(materialData)` - Extract OpenPBR subset
- `extractUsdPreviewSurfaceData(materialData)` - Extract UsdPreviewSurface subset
- `extractThreeMaterialData(threeMaterial)` - Extract Three.js properties
- `syntaxHighlightJSON(json)` - Apply color coding

### Data Flow

```
USD File
    ↓
TinyUSDZ Parser
    ↓
Tydra Conversion
    ↓
Material Data Object ← [RAW TAB]
    ├─→ OpenPBR Data ← [OPENPBR TAB]
    └─→ UsdPreviewSurface Data ← [USDPREVIEW TAB]
    ↓
Three.js Material ← [THREEJS TAB]
    ↓
WebGL Rendering
```

### Integration with Material Selection

When an object or material is selected:

```javascript
// In selectObject() or selectMaterial()
window.selectedMaterialForExport = material;

// JSON viewer reads from global
const selectedMaterial = window.selectedMaterialForExport;
showMaterialJSON(selectedMaterial);
```

### Texture ID Handling

Texture references in the JSON show as:
```json
"base_color": {
  "textureId": 5,
  "value": [1.0, 1.0, 1.0]
}
```

The Three.js tab shows loaded textures as:
```json
"textures": {
  "map": "Texture(123)"  // Three.js texture ID
}
```

## Keyboard Shortcuts

Currently no keyboard shortcuts implemented, but could add:
- `ESC` - Close viewer
- `Ctrl+C` - Copy current tab
- `1-4` - Switch between tabs

## Browser Compatibility

- **Copy to Clipboard**: Requires modern browser with Clipboard API
- **Syntax Highlighting**: Works in all browsers (pure CSS/JavaScript)
- **Download**: Works in all modern browsers

## Performance

- **Small materials** (<100 properties): Instant display
- **Large materials** (>500 properties): Minor lag on tab switch
- **Very large materials** (>2000 properties): May see scrolling lag

The viewer is optimized for typical material sizes (50-200 properties).

## Comparison with Other Tools

| Feature | Material JSON Viewer | Browser DevTools | External JSON Viewer |
|---------|---------------------|------------------|---------------------|
| Syntax highlighting | ✓ | ✓ | ✓ |
| Material-specific tabs | ✓ | ✗ | ✗ |
| One-click copy | ✓ | Partial | ✓ |
| Material context | ✓ | ✗ | ✗ |
| Three.js integration | ✓ | ✗ | ✗ |
| No external tool needed | ✓ | ✓ | ✗ |

## Future Enhancements

Potential improvements:
- [ ] Search/filter within JSON
- [ ] Collapse/expand nested objects
- [ ] Dark/light theme toggle
- [ ] Diff view (compare two materials)
- [ ] Edit mode (modify values and apply)
- [ ] Export to MaterialX XML directly
- [ ] Export to YAML/TOML formats
- [ ] Pretty-print options (compact/expanded)
- [ ] Keyboard shortcuts
- [ ] History (view previous materials)
- [ ] Pin multiple materials for comparison

## Troubleshooting

**"No material selected"**
- Select an object by clicking it in the 3D view, or
- Select a material from the Materials panel

**"No OpenPBR data available"**
- Material doesn't have OpenPBR definition
- Try the UsdPreviewSurface tab instead
- Check Raw Material Data to see what's available

**"No UsdPreviewSurface data available"**
- Material doesn't have UsdPreviewSurface definition
- Try the OpenPBR tab instead
- Material might only have one type defined

**Copy to clipboard doesn't work**
- Browser might not support Clipboard API
- Try Download JSON instead
- Check browser console for errors

**JSON looks incorrect**
- Verify material loaded correctly
- Check Raw Material Data tab for source
- Report issue with both Raw and converted data

## Related Documentation

- **[OpenPBR Parameters Reference](../../../doc/openpbr-parameters-reference.md)** - OpenPBR parameter details
- **[MaterialX Support](../../../doc/materialx.md)** - Blender MaterialX export mapping
- **[Node Graph Viewer](./README-node-graph.md)** - Visual material graph
- **[TinyUSDZ API](../../../README.md)** - USD loading and parsing

## License

Part of the TinyUSDZ project (Apache 2.0 License).
