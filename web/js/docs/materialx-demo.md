# MaterialX / OpenPBR Web Demos

Web-based MaterialX/OpenPBR material viewers built on TinyUSDZ WASM + Three.js.

## Demo Entry Points

### materialx.js (Basic MaterialX Demo)

**Entry**: `materialx.html` → `materialx.js`

Simple OpenPBR demo with Three.js `MeshPhysicalMaterial`. Features:

- USD file loading (USDA/USDC/USDZ) via TinyUSDZ WASM
- OpenPBR parameter editing (base, specular, transmission, coat, emission, geometry, subsurface, thin film)
- Texture loading from USD with colorspace handling (sRGB, Linear, Rec.709, ACES AP0/AP1)
- MaterialX XML import/export, JSON export
- Synthetic HDR environments (studio, all-white)
- Tone mapping (ACES 2.0, Cineon, AgX, Neutral)
- Display-P3 wide gamut support
- PBR debugging tools: color picker, material property picker, material JSON viewer, split-view comparison, material validator, texture inspector

**Key modules**: `TinyUSDZMaterialX.js`, `OpenPBRMaterial.js`, `OpenPBRValidation.js`, `color-picker.js`, `material-property-picker.js`, `material-json-viewer.js`, `material-override.js`, `split-view-comparison.js`, `texture-inspector.js`

### openpbr-nodegraph-demo.js (NodeGraph Demo)

**Entry**: `openpbr-nodegraph-demo.html` → `openpbr-nodegraph-demo.js`

Advanced OpenPBR demo with LiteGraph.js node graph visualization and WebGL2 MaterialX node graph evaluation. Features:

- Everything in the basic demo, plus:
- LiteGraph-based visual node graph editor for MaterialX shader networks
- MaterialX node graph optimization (dead code elimination, constant folding)
- Blender MaterialX export support via `TinyUSDZOpenPBR_WebGL.js`
- Custom GLSL shader injection via `onBeforeCompile`
- Per-material node graph selector
- Blender-style material shading panels
- Undo/redo history (20 steps)
- Demand rendering, lazy LiteGraph draw, shader pre-warm
- Normal map support, colorspace conversion nodes
- Bridge client integration (Blender-bridge)

**Key modules**: `TinyUSDZOpenPBR_WebGL.js` (WebGL2 MaterialX node eval), `materialx-node-graph.js` (LiteGraph integration)

### Other MaterialX Demos

| Demo | Entry | Purpose |
|------|-------|---------|
| `materialx-webgl2.js` | `materialx-webgl2.html` | Blender MaterialX exports with WebGL2 node graph optimization |
| `materialx-webgpu.js` | `materialx-webgpu.html` | WebGPU renderer with Three.js TSL (Three Shading Language) |
| `mtlx-webgpu.js` | `mtlx-webgpu.html` | Standalone WebGPU MaterialX demo |
| `mtlx-debug.js` | `mtlx-debug.html` | Debug-focused MaterialX viewer with synthetic HDR |

## Dual Material System

Materials can be rendered via two paths:

```
TinyUSDZ OpenPBR Data
       ↓
  [Toggle Check]
       ↓
 ┌─────┴──────┐
 │             │
 ▼             ▼
MeshPhysical   NodeMaterial
Material       (MaterialX)
(Manual Map)   (WebGPU/TSL)
```

- **MeshPhysicalMaterial** (default): Direct OpenPBR → Three.js property mapping, works with WebGL
- **NodeMaterial**: Via `MaterialXLoader` or custom `TinyUSDZOpenPBR_TSL.js`, uses TSL for WebGPU

## Node Graph Visualization (LiteGraph.js)

Custom MaterialX node types registered with LiteGraph:

| Node Type | Color | Purpose |
|-----------|-------|---------|
| OpenPBR Surface | Purple | Main shader with all OpenPBR inputs |
| Image/Texture | Green | Texture maps with colorspace info |
| Constant Color | Yellow | Fixed color values |
| Constant Float | Blue | Numeric parameters |
| Material Output | Pink | Final shader output |

Navigation: pan (drag background), zoom (scroll), center (button), close (ESC).

## CLI Tools

### dump-materialx-cli.js

Dump MaterialX RenderMaterial data from USD files.

```bash
npx vite-node dump-materialx-cli.js <usd-file> [options]
  -f, --format <format>   json | yaml | xml (default: json)
  -o, --output <file>     Write to file instead of stdout
  -m, --material <id>     Specific material by ID (default: all)
  -v, --verbose           Enable verbose logging
```

### dump-mtlx-node.js / optimize-mtlx-node.js

Node graph structure inspection and optimization (DCE, constant folding).

### convert-openpbr-to-mtlx.js

Convert OpenPBR material data to MaterialX 1.38 XML string.

## Verification System

Automated visual regression testing using headless Chrome + pixelmatch.

```bash
npm run test:colorspace          # Pure Node.js colorspace tests (no GPU)
npm run verify-materialx render  # Render + compare against reference
npm run verify-materialx clean   # Remove results
```

**Pipeline**: Launch headless Chrome (SwiftShader) → load test HTML pages → render 120 frames at 800x600 → screenshot → pixelmatch comparison → HTML report.

**Pass criteria**: < 2% pixel difference. Default test materials: brass, glass, gold, copper, plastic, marble.

**Files**:
- `verify-materialx.js` — main CLI tool
- `tests/colorspace-test.js` — sRGB ↔ Linear, Rec.709 → XYZ tests
- `tests/render-tinyusdz.html` / `tests/render-reference.html` — test renderers

## OpenPBR → Three.js Parameter Mapping

| OpenPBR | Three.js MeshPhysicalMaterial |
|---------|-------------------------------|
| `base.color` | `color` |
| `base.metalness` | `metalness` |
| `specular.roughness` | `roughness` |
| `specular.ior` | `ior` |
| `transmission.weight` | `transmission` |
| `coat.weight` | `clearcoat` |
| `coat.roughness` | `clearcoatRoughness` |
| `emission.color` | `emissive` |
| `emission.intensity` | `emissiveIntensity` |

## Limitations

- Subsurface scattering approximated via transmission (Three.js limitation)
- Thin film stored but not fully visualized
- Sheen has limited support in MeshPhysicalMaterial
- Procedural textures not supported (image-based only)
- NodeMaterial texture mapping is limited

## Setup

```bash
cd web && ./bootstrap-linux-wasm64.sh && cd build && make -j8
cd web/js && npm install && npm run dev
# Open http://localhost:5173/materialx.html
```
