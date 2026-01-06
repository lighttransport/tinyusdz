# Three.js MaterialX Support and TinyUSDZ/Tydra Integration

## Executive Summary

This document describes the current state of MaterialX support in Three.js and the requirements for Tydra to bridge USD MaterialX settings to Three.js rendering. Three.js has experimental MaterialX support through WebGPU renderer with a MaterialXLoader, while TinyUSDZ/Tydra provides comprehensive MaterialX material conversion capabilities.

## Three.js MaterialX Support (2024-2025)

### Current Implementation Status

#### WebGPU MaterialX Loader
- **File**: `examples/webgpu_loader_materialx.html`
- **Status**: Experimental/Example stage
- **Renderer**: WebGPU only (not WebGL/WebGL2)
- **Features**:
  - Loads `.mtlx` files directly
  - Supports material preview on shader ball models
  - Dynamic material switching with GUI controls

#### Supported MaterialX Features

1. **Standard Surface Materials**
   - Brass, chrome, gold, and other metallic surfaces
   - Procedural textures and patterns
   - Material transformations
   - Opacity and transmission effects
   - Thin film rendering
   - Sheen and roughness properties

2. **Node Support** (PR #31439 improvements)
   - Mathematical nodes: `ln`, `transform`, `matrix`, `transpose`, `determinant`, `invert`
   - Geometric nodes: `length`, `crossproduct`, `reflect`, `refract`
   - Conditional nodes: `ifgreater`, `ifequal`
   - Noise nodes: `uniformnoise2d`, `uniformnoise3d`
   - Procedural nodes: `ramp4`, `place2d`
   - Color transformations
   - Screen-space derivative calculations
   - Smoothstep and mixing operations

3. **Material Properties**
   - **Standard Surface**: Full support for opacity, specular intensity/color
   - **Advanced Properties**: IOR, anisotropy, sheen, thin film
   - **Transmission**: Color and thickness support
   - **Rendering Modes**: Automatic transparency and double-sided rendering

### Technical Implementation

#### Three.js Node Material System
- Uses TSL (Three Shading Language) for node-based material authoring
- Transpiles to GLSL or WGSL depending on renderer
- Goal: Compose any MaterialX node from <5 Three.js nodes
- Aligns with Blender's MaterialX exporter for compatibility

#### Limitations
- WebGPU-only support (experimental API, limited browser support)
- No WebGL/WebGL2 fallback currently
- Complex nodes like convolution ("blur", "heighttonormal") are challenging
- MaterialX WASM library integration still exploratory

## TinyUSDZ MaterialX Implementation

### Current Architecture

#### Core Components

1. **MaterialXConfigAPI** (`src/usdShade.hh`)
   ```cpp
   struct MaterialXConfigAPI {
     TypedAttributeWithFallback<std::string> mtlx_version{"1.38"};
     TypedAttributeWithFallback<std::string> mtlx_namespace{""};
     TypedAttributeWithFallback<std::string> mtlx_colorspace{""};
     TypedAttributeWithFallback<std::string> mtlx_sourceUri{""};
   };
   ```

2. **Supported Shader Models**
   - `MtlxUsdPreviewSurface`: MaterialX-extended UsdPreviewSurface
   - `MtlxAutodeskStandardSurface`: Autodesk Standard Surface (v1.0.1)
   - `OpenPBRSurface`: Academy Software Foundation OpenPBR model

3. **File Format Support**
   - Direct `.mtlx` file loading
   - USD references to MaterialX files: `@myshader.mtlx@`
   - Embedded MaterialX in USD files

### Tydra Conversion Pipeline

#### Current Capabilities

1. **Material Conversion Flow**
   ```
   USD Stage → Material with MaterialXConfigAPI → Tydra RenderMaterial
   ```

2. **Dual Material Support**
   ```cpp
   class RenderMaterial {
     nonstd::optional<PreviewSurfaceShader> surfaceShader;  // UsdPreviewSurface
     nonstd::optional<OpenPBRSurfaceShader> openPBRShader;  // MaterialX OpenPBR
   };
   ```

3. **OpenPBRSurfaceShader** (`src/tydra/render-data.hh`)
   - Base layer: weight, color, roughness, metalness
   - Specular layer: weight, color, roughness, IOR, anisotropy
   - Subsurface scattering parameters
   - Coat layer properties
   - Thin film effects
   - Emission properties
   - Geometry modifiers (normal, tangent, displacement)

## Requirements for Tydra to Three.js Bridge

### 1. Material Export Module

**Purpose**: Convert Tydra's RenderMaterial to Three.js-compatible format

**Requirements**:
```cpp
class ThreeJSMaterialExporter {
public:
  // Export to Three.js MaterialX format
  std::string ExportToMaterialX(const RenderMaterial& material);

  // Export to Three.js JSON format with node graph
  json ExportToNodeMaterial(const RenderMaterial& material);

  // Map Tydra shader params to Three.js nodes
  json ConvertShaderParams(const OpenPBRSurfaceShader& shader);
};
```

### 2. Node Graph Translation

**Mapping Requirements**:

| TinyUSDZ/Tydra | Three.js Node | Notes |
|----------------|---------------|-------|
| OpenPBRSurface.base_color | standard_surface.base_color | Direct mapping |
| OpenPBRSurface.base_metalness | standard_surface.metalness | Direct mapping |
| OpenPBRSurface.specular_weight | standard_surface.specular | May need scaling |
| OpenPBRSurface.coat_weight | standard_surface.coat | Direct mapping |
| UsdUVTexture | texture2d + place2d | Combine nodes |
| Transform2d | place2d | UV transformations |

### 3. Texture Handling

**Requirements**:
- Convert Tydra's texture references to Three.js texture loader format
- Handle UV transformations (scale, rotate, offset)
- Support for MaterialX colorspace tags
- Texture channel packing (e.g., ORM textures)

### 4. WebGPU vs WebGL Compatibility

**Dual Output Strategy**:
1. **WebGPU Path**: Direct MaterialX node graph export
2. **WebGL Fallback**: Convert to standard Three.js materials
   ```javascript
   // WebGL fallback
   const material = new THREE.MeshPhysicalMaterial({
     color: materialData.base_color,
     metalness: materialData.base_metalness,
     roughness: materialData.base_roughness,
     // ... mapped properties
   });
   ```

### 5. Implementation Phases

#### Phase 1: Basic Material Export
- Export OpenPBRSurface to Three.js MeshPhysicalMaterial
- Basic property mapping (color, metalness, roughness)
- Texture reference export

#### Phase 2: Advanced Features
- Full OpenPBR parameter support
- UV transformation handling
- Multi-layer material support (base + coat + sheen)

#### Phase 3: Node Graph Export
- Generate Three.js TSL node graphs
- Support procedural textures
- Custom node implementations

#### Phase 4: Optimization
- Texture atlas generation
- Material deduplication
- LOD material variants

## API Design Proposal

### Tydra Extension API

```cpp
namespace tinyusdz {
namespace tydra {

class ThreeJSExporter {
public:
  struct ExportOptions {
    bool use_webgpu = true;        // Target WebGPU renderer
    bool generate_fallback = true;  // Generate WebGL fallback
    bool embed_textures = false;    // Embed texture data in JSON
    std::string texture_path = "";  // External texture directory
  };

  // Export entire RenderScene
  bool ExportScene(const RenderScene& scene,
                   const ExportOptions& options,
                   std::string& json_output);

  // Export single material
  bool ExportMaterial(const RenderMaterial& material,
                     const ExportOptions& options,
                     std::string& json_output);

  // Generate MaterialX document
  bool ExportMaterialX(const RenderMaterial& material,
                      std::string& mtlx_output);
};

} // namespace tydra
} // namespace tinyusdz
```

### Three.js Import API

```javascript
// Three.js side import
import { TinyUSDZMaterialLoader } from 'three/addons/loaders/TinyUSDZMaterialLoader.js';

const loader = new TinyUSDZMaterialLoader();

// Load exported material
loader.load('exported_material.json', (material) => {
  mesh.material = material;
});

// Or direct MaterialX
const mtlxLoader = new MaterialXLoader();
mtlxLoader.load('exported.mtlx', (material) => {
  mesh.material = material;
});
```

## Testing Strategy

### Test Materials
1. **Basic PBR**: Simple metallic/dielectric materials
2. **Layered**: Materials with coat, sheen, subsurface
3. **Textured**: Materials with multiple texture maps
4. **Procedural**: Materials with noise and patterns
5. **Transmission**: Glass and translucent materials

### Validation Process
1. Export material from USD/MaterialX via Tydra
2. Import in Three.js WebGPU renderer
3. Visual comparison with reference renders
4. Performance profiling

## Performance Considerations

### Optimization Opportunities
1. **Material Batching**: Combine similar materials
2. **Texture Atlasing**: Pack multiple textures
3. **Shader Caching**: Reuse compiled shaders
4. **LOD System**: Simplified materials for distant objects

### Memory Management
1. **Texture Compression**: Use GPU-friendly formats
2. **On-demand Loading**: Lazy load textures
3. **Resource Pooling**: Share common resources

## Conclusion

The integration of TinyUSDZ/Tydra MaterialX support with Three.js is feasible and valuable. The proposed implementation would:

1. Enable full MaterialX material export from USD to Three.js
2. Support both WebGPU (native) and WebGL (fallback) rendering
3. Provide a production-ready pipeline for USD to web visualization
4. Maintain compatibility with industry-standard tools

The phased approach allows incremental development while delivering value at each stage. The dual-path strategy (WebGPU/WebGL) ensures broad compatibility while leveraging modern capabilities where available.

## References

- [MaterialX Specification v1.38](https://www.materialx.org/docs/api/index.html)
- [Three.js MaterialX Support (Issue #20541)](https://github.com/mrdoob/three.js/issues/20541)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
- [Three.js WebGPU MaterialX Loader Example](https://threejs.org/examples/webgpu_loader_materialx.html)
- [TinyUSDZ Documentation](https://github.com/syoyo/tinyusdz)