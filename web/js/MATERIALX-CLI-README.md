# MaterialX RenderMaterial Dump CLI

A Node.js command-line tool for dumping MaterialX RenderMaterial data from USD/USDA/USDC/USDZ files.

## Features

- Export materials in JSON, YAML, or MaterialX XML format
- Human-readable YAML output for easy inspection and editing
- Dump all materials or specific material by ID
- Support for OpenPBR Surface and UsdPreviewSurface materials
- Pretty-printed JSON/YAML output
- File or stdout output
- Verbose logging mode

## Installation

```bash
cd web/js
npm install
```

## Usage

### Basic Usage

```bash
# Dump all materials as JSON
npm run dump-materialx ../../models/polysphere-materialx-001.usda

# Using node directly
node dump-materialx-cli.js ../../models/suzanne-pbr.usda
```

### Command Line Options

```
Usage: node dump-materialx-cli.js <usd-file> [options]

Arguments:
  <usd-file>              USD/USDA/USDC/USDZ file to load

Options:
  -f, --format <format>   Output format: 'json', 'yaml', or 'xml' (default: json)
  -o, --output <file>     Write output to file instead of stdout
  -m, --material <id>     Dump only specific material by ID (default: all)
  --no-pretty             Disable pretty-printing for JSON/YAML
  -v, --verbose           Enable verbose logging
  -h, --help              Show this help message
```

### Examples

#### Dump all materials as JSON

```bash
npm run dump-materialx ../../models/polysphere-materialx-001.usda
```

Output:
```json
[
  {
    "name": "Material_0",
    "hasOpenPBR": true,
    "hasUsdPreviewSurface": false,
    "openPBR": {
      "base": {
        "weight": { "value": 1.0, "textureId": -1 },
        "color": { "value": [0.8, 0.8, 0.8], "textureId": -1 },
        "roughness": { "value": 0.5, "textureId": -1 },
        "metalness": { "value": 0.0, "textureId": -1 }
      },
      ...
    }
  }
]
```

#### Dump all materials as YAML (Human-Readable)

```bash
npm run dump-materialx ../../models/suzanne-pbr.usda -- -f yaml
```

Output:
```yaml
- name: "Material_0"
  hasOpenPBR: true
  hasUsdPreviewSurface: false
  openPBR:
    base:
      weight:
        value: 1
        textureId: -1
      color:
        value:
          - 0.8
          - 0.8
          - 0.8
        textureId: -1
      roughness:
        value: 0.5
        textureId: -1
      metalness:
        value: 0
        textureId: -1
    specular:
      weight:
        value: 1
        textureId: -1
      color:
        value:
          - 1
          - 1
          - 1
        textureId: -1
      ior:
        value: 1.5
        textureId: -1
    emission:
      luminance:
        value: 0
        textureId: -1
      color:
        value:
          - 1
          - 1
          - 1
        textureId: -1
    geometry:
      opacity:
        value: 1
        textureId: -1
```

#### Dump specific material as MaterialX XML

```bash
npm run dump-materialx ../../models/suzanne-pbr.usda -- -f xml -m 0
```

Output:
```xml
<?xml version="1.0"?>
<materialx version="1.38">
  <open_pbr_surface name="Material_0_surface" type="surfaceshader">
    <input name="base_weight" type="float" value="1.0" />
    <input name="base_color" type="color3" value="0.8, 0.8, 0.8" />
    <input name="base_roughness" type="float" value="0.5" />
    ...
  </open_pbr_surface>
  <surfacematerial name="Material_0" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="Material_0_surface" />
  </surfacematerial>
</materialx>
```

#### Save output to file with verbose logging

```bash
npm run dump-materialx ../../models/teapot-pbr.usdc -- -o materials.json -v
```

#### Dump multiple materials as XML to file

```bash
npm run dump-materialx ../../models/texturedcube.usdc -- -f xml -o materials.xml -v
```

## Output Formats

### JSON Format

The JSON output includes compact or pretty-printed JSON data:

- **name**: Material name from USD
- **hasOpenPBR**: Boolean indicating if OpenPBR shader data is present
- **hasUsdPreviewSurface**: Boolean indicating if UsdPreviewSurface data is present
- **openPBR**: Complete OpenPBR surface shader parameters
  - **base**: Base layer (color, roughness, metalness)
  - **specular**: Specular layer (weight, color, IOR)
  - **transmission**: Transmission properties
  - **subsurface**: Subsurface scattering
  - **sheen**: Sheen layer
  - **coat**: Clear coat layer
  - **emission**: Emission properties
  - **geometry**: Geometry modifiers (opacity, normal, tangent)
- **usdPreviewSurface**: UsdPreviewSurface shader parameters (if present)

Each parameter includes:
- `value`: The numeric value(s)
- `textureId`: Texture ID if texture-mapped (-1 if not)

### YAML Format

The YAML output provides a human-readable format that is:
- **Easy to read**: Clear hierarchical structure with indentation
- **Easy to edit**: Can be manually edited and converted back to JSON
- **Easy to diff**: Version control friendly for tracking changes
- **Same structure as JSON**: Contains all the same data as JSON format

YAML is ideal for:
- Manual inspection and review of material parameters
- Documenting material configurations
- Sharing material data in a readable format
- Debugging material issues

The YAML structure is identical to JSON but uses YAML syntax for better readability.

### XML Format (MaterialX)

The XML output is a valid MaterialX document that can be:
- Imported into MaterialX-compatible renderers
- Used in DCC tools that support MaterialX
- Converted to other material representations

## Integration with Three.js

The JSON or YAML output can be directly used to create Three.js materials:

```javascript
import fs from 'fs';
import YAML from 'yaml';

// Load the dumped material data (JSON)
const materials = JSON.parse(fs.readFileSync('materials.json', 'utf8'));

// Or from YAML
// const materials = YAML.parse(fs.readFileSync('materials.yaml', 'utf8'));

// Convert to Three.js MeshPhysicalMaterial
for (const mat of materials) {
  if (mat.hasOpenPBR) {
    const openPBR = mat.openPBR;
    const threeMaterial = new THREE.MeshPhysicalMaterial({
      color: new THREE.Color(...openPBR.base.color.value),
      metalness: openPBR.base.metalness.value,
      roughness: openPBR.base.roughness.value,
      clearcoat: openPBR.coat.weight.value,
      clearcoatRoughness: openPBR.coat.roughness.value,
      transmission: openPBR.transmission.weight.value,
      // ... more properties
    });
  }
}
```

## Technical Details

### Memory Requirements

The CLI sets a default memory limit of 500MB. For very large USD files, you may need to modify this in the source code.

### Supported USD Formats

- **USDA**: ASCII USD files
- **USDC**: Binary (Crate) USD files
- **USDZ**: ZIP-archived USD packages

### Material Types Supported

- **OpenPBR Surface**: Full OpenPBR shader specification
- **UsdPreviewSurface**: USD's built-in preview surface shader

## Troubleshooting

### "File not found" error

Make sure the path to the USD file is correct. Use relative paths from the web/js directory.

### "No materials found" warning

The USD file may not contain any materials. Check that the file has material definitions.

### Memory errors

For large USD files, you may need to increase the memory limit in the CLI source code:

```javascript
loader.setMaxMemoryLimitMB(1000); // Increase from default 500
```

## Development

### Running without npm

```bash
node dump-materialx-cli.js <options>
```

### Debugging

Set the DEBUG environment variable to see stack traces:

```bash
DEBUG=1 npm run dump-materialx <options>
```

## Related Files

- `test-openpbr-material.js` - Browser-based material testing example
- `load-test-node.js` - Node.js USD loading test
- `../binding.cc` - WASM bindings (contains getMaterialWithFormat implementation)

## License

Apache 2.0 - See repository LICENSE file
