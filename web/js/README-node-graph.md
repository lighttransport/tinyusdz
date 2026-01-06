# MaterialX Node Graph Viewer

A lightweight, interactive node graph visualization system for MaterialX/OpenPBR materials in the TinyUSDZ web demo.

## Overview

The Node Graph Viewer provides a Blender-style node editor interface to visualize and understand MaterialX shader networks. It uses [LiteGraph.js](https://github.com/jagenjo/litegraph.js), a permissive MIT-licensed graph node engine.

## Features

- **Visual Node Network**: See the complete shader network including textures, parameters, and connections
- **OpenPBR Surface Support**: Displays OpenPBR materials with all layers (base, specular, transmission, coat, emission)
- **Interactive Navigation**: Pan, zoom, and explore complex material graphs
- **Automatic Layout**: Smart node positioning for clarity
- **Export**: Save node graphs as JSON for documentation or external tools

## Usage

### Opening the Node Graph

1. Load a USD file with MaterialX materials
2. Select a material from the Materials panel (bottom-left)
3. Click the **🔗 Node Graph** button in the top toolbar
4. The node graph viewer will open in a full-screen overlay

### Navigation

- **Pan**: Click and drag the canvas background
- **Zoom**: Scroll mouse wheel or pinch trackpad
- **Center View**: Click the "Center" button
- **Close**: Click the "Close" button or press ESC

### Node Types

#### OpenPBR Surface (Purple)
The main shader node with inputs for all OpenPBR parameters:
- Base Color, Weight, Metalness, Roughness
- Specular Weight, Color, Roughness, IOR, Anisotropy
- Transmission Weight, Color
- Coat Weight, Color, Roughness, IOR
- Emission Color, Luminance
- Opacity, Normal

#### Image/Texture (Green)
Represents texture maps with:
- File path display
- Color space information
- RGB and alpha channel outputs

#### Constant Color (Yellow/Orange)
Fixed color values with visual preview

#### Constant Float (Blue)
Numeric parameter values

#### Material Output (Pink)
Final shader output node

### Node Graph Controls

**In the header:**
- **Center**: Reset view to show all nodes
- **Export JSON**: Save the graph structure
- **Close**: Exit the node graph viewer

**In the info panel (bottom-left):**
- Current zoom level
- Total node count
- Keyboard shortcuts reminder

## Technical Details

### Implementation

The node graph system is split into two files:

1. **materialx-node-graph.js**: Standalone module with all node graph logic
   - Node type definitions
   - Graph creation from MaterialX data
   - UI management
   - Export functionality

2. **materialx.js**: Main application with integration
   - Imports and initializes the node graph module
   - Provides material data to visualizer
   - Handles user interactions

### Node Graph Data Structure

The graph is constructed from the MaterialX/OpenPBR data structure:

```javascript
{
  hasOpenPBR: true,
  name: "MaterialName",
  openPBR: {
    base: { color: [r,g,b], weight: 1.0, metalness: 0.0, ... },
    specular: { roughness: 0.3, ior: 1.5, ... },
    transmission: { weight: 0.0, ... },
    coat: { weight: 0.0, ... },
    emission: { color: [r,g,b], luminance: 0.0 },
    geometry: { opacity: 1.0, ... }
  }
}
```

### LiteGraph.js Integration

LiteGraph.js is loaded via CDN:
```html
<script src="https://cdn.jsdelivr.net/npm/litegraph.js@0.7.0/build/litegraph.js"></script>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/litegraph.js@0.7.0/css/litegraph.css">
```

Custom MaterialX node types are registered at initialization:
```javascript
LiteGraph.registerNodeType("materialx/openpbr_surface", OpenPBRSurfaceNode);
LiteGraph.registerNodeType("materialx/image", ImageNode);
LiteGraph.registerNodeType("materialx/constant_color", ConstantColorNode);
// ... etc
```

## Customization

### Adding New Node Types

To add support for additional MaterialX nodes:

1. Define the node class in `materialx-node-graph.js`:
```javascript
function MyCustomNode() {
    this.addInput("Input", "type");
    this.addOutput("Output", "type");
    this.properties = { value: 0 };
    this.color = "#HEXCOLOR";
    this.size = [width, height];
}
MyCustomNode.title = "My Node";
MyCustomNode.desc = "Description";
```

2. Register it:
```javascript
LiteGraph.registerNodeType("materialx/my_node", MyCustomNode);
```

3. Add creation logic in `createOpenPBRGraph()` or `createUsdPreviewSurfaceGraph()`

### Styling

Node colors can be customized via the `color` property:
- Purple (`#673AB7`): Shader nodes
- Green (`#4CAF50`): Textures
- Yellow (`#FFC107`): Colors
- Blue (`#03A9F4`): Floats
- Pink (`#E91E63`): Outputs

CSS overrides in `materialx.html` control the overall appearance.

## Browser Compatibility

- **Modern browsers**: Chrome, Firefox, Edge, Safari (latest versions)
- **Requirements**: ES6 modules, Canvas API
- **Recommended**: Hardware acceleration enabled

## Performance

- **Light graphs** (<20 nodes): Smooth 60fps interaction
- **Medium graphs** (20-50 nodes): Good performance
- **Large graphs** (>50 nodes): May see minor lag on zoom/pan

## Future Enhancements

Potential improvements for future versions:

- [ ] Node editing (change parameter values)
- [ ] Real-time material preview on parameter changes
- [ ] MaterialX XML import/export from node graph
- [ ] Custom node creation UI
- [ ] Node search and filtering
- [ ] Mini-map for large graphs
- [ ] Node grouping/collapse
- [ ] Animation of data flow
- [ ] Support for more MaterialX node types (math, patterns, etc.)
- [ ] UsdPreviewSurface full visualization
- [ ] Blender MaterialX export comparison mode

## License

This implementation uses:
- **LiteGraph.js**: MIT License
- **TinyUSDZ**: Apache 2.0 License

The node graph visualization code is part of the TinyUSDZ project.

## References

- [LiteGraph.js Documentation](https://github.com/jagenjo/litegraph.js)
- [MaterialX Specification](https://materialx.org/)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [Blender MaterialX Documentation](doc/materialx.md)
