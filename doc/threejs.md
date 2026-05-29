# Three.js Integration Notes

Notes on Three.js animation and MaterialX systems relevant to Tydra RenderScene conversion and the TinyUSDZ JavaScript viewer.

## Animation System

### Three.js Animation Hierarchy

```
Keyframes (raw data)
    -> KeyframeTrack (property animation)
    -> AnimationClip (collection of tracks)
    -> AnimationMixer (playback control)
```

### KeyframeTrack Types

- **VectorKeyframeTrack**: For `position` and `scale` (3D vectors)
- **QuaternionKeyframeTrack**: For `rotation` (quaternions, NOT Euler angles)
- **NumberKeyframeTrack**: For scalar values or morph targets

### glTF to Three.js Mapping

| glTF Path | Three.js Track Type | Three.js Property |
|-----------|-------------------|-------------------|
| `translation` | VectorKeyframeTrack | `.position` |
| `rotation` | QuaternionKeyframeTrack | `.quaternion` |
| `scale` | VectorKeyframeTrack | `.scale` |
| `weights` | NumberKeyframeTrack | `.morphTargetInfluences[i]` |

### Interpolation Modes

1. **STEP** (`InterpolateDiscrete`): No interpolation
2. **LINEAR** (`InterpolateLinear`): Linear (slerp for quaternions)
3. **CUBICSPLINE**: Cubic spline with tangents (custom in GLTFLoader)

### Key Points

- All angles in radians, default Euler order `'XYZ'`
- Keyframe data stored in flat arrays: `[x0,y0,z0, x1,y1,z1, ...]`
- Time values in seconds (floating point)
- Always prefer quaternions over Euler angles (avoids gimbal lock)

### Tydra Data Structure

Tydra's actual IR splits keyframe data (`KeyframeSampler`: flat `times`/`values`
+ `AnimationInterpolation`) from bindings (`AnimationChannel`: `path`,
`target_type`, node/joint indices, `sampler`), following the glTF model. See
`src/tydra/render-data.hh` and [doc/tydra-animation-spec-en.md](tydra-animation-spec-en.md)
for the full structures and the Three.js track export.

## MaterialX Support

### Three.js MaterialXLoader Status (2024-2025)

Three.js's own `MaterialXLoader` is **WebGPU only** (experimental): it supports
Standard Surface materials, procedural textures, and noise nodes via TSL (Three
Shading Language) for node-based authoring, with no WebGL/WebGL2 fallback.

The TinyUSDZ viewer does **not** rely on `MaterialXLoader`. It ships both a
WebGPU demo (`web/js/materialx-webgpu.js`, TSL `NodeMaterial`) and a WebGL2 demo
(`web/js/materialx-webgl2.js`, a custom OpenPBR WebGL material via
`TinyUSDZOpenPBR_WebGL.js`) — see the WebGL fallback strategy below.

### TinyUSDZ MaterialX Architecture

Supported shader models:
- `MtlxUsdPreviewSurface` (`src/usdMtlx.hh`): MaterialX-extended UsdPreviewSurface
- `MtlxAutodeskStandardSurface` (`src/usdMtlx.hh`): Autodesk Standard Surface
- `OpenPBRSurface` (`src/usdShade.hh`): Academy Software Foundation OpenPBR model

File format support: direct `.mtlx` loading, USD references (`@myshader.mtlx@`), embedded MaterialX.

### Tydra Conversion Pipeline

```
USD Stage -> Material with MaterialXConfigAPI -> Tydra RenderMaterial
```

Dual material output:
```cpp
class RenderMaterial {
    nonstd::optional<PreviewSurfaceShader> surfaceShader;   // UsdPreviewSurface
    nonstd::optional<OpenPBRSurfaceShader> openPBRShader;   // MaterialX OpenPBR
};
```

### Property Mapping (Tydra -> Three.js)

| TinyUSDZ/Tydra | Three.js | Notes |
|----------------|----------|-------|
| OpenPBRSurface.base_color | standard_surface.base_color | Direct |
| OpenPBRSurface.base_metalness | standard_surface.metalness | Direct |
| OpenPBRSurface.specular_weight | standard_surface.specular | May need scaling |
| OpenPBRSurface.coat_weight | standard_surface.coat | Direct |
| UsdUVTexture | texture2d + place2d | Combine nodes |

### WebGL Fallback Strategy

For WebGL, convert to `MeshPhysicalMaterial`:
```javascript
const material = new THREE.MeshPhysicalMaterial({
    color: materialData.base_color,
    metalness: materialData.base_metalness,
    roughness: materialData.base_roughness,
});
```

## References

- [Three.js Animation System](https://threejs.org/docs/#manual/en/introduction/Animation-system)
- [glTF 2.0 Animation Spec](https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#animations)
- [MaterialX Specification](https://www.materialx.org/docs/api/index.html)
- [OpenPBR Specification](https://github.com/AcademySoftwareFoundation/OpenPBR)
