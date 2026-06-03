# What is Tydra?

![Tydra](tydra.png)

TinyUSDZ does not support Hydra interface in pxrUSD at the moment.
We think Hydra(multi-purpose sceneDelegate/renderDelegate interface) is too much for TinyUSDZ usecases(AR, lightweight 3D viewer/runtime, DCC exchange, etc).

Instead, we'd like to propose Tydra(Tiny Hydra), something like a three-headed monster(Please imagine Gidorah: https://en.wikipedia.org/wiki/King_Ghidorah), which directly converts(`publishes`) USD scene graph(Stage. Prim hierarchy) to a renderer-friendly data structure or `published` data format(imagine glTF). API design of Tydra is completely different from Hydra.

Currently Tydra is considering following three usecases in mind:

- Runtime publishment(e.g. to glTF), DCC conversion and exchange for realtime graphics(AR, VR, MR, games, etc).
- [x] Scene conversion to GL/Vulkan renderer(e.g. WebGL rendering)
  See `../../examples/tydra_to_renderscene`
- [ ] Scene conversion to Ray tracing renderer(e.g. Vulkan/OptiX ray tracing)
  See `../../examples/sdlviewer/` for SW raytracing example.

## Status

Production-ready for OpenGL/Vulkan/Three.js rendering pipelines. API is mostly stable but may still evolve.

## RenderScene

Scene graph representation suited for OpenGL/Vulkan/WebGL renderer.

### Status

* [x] Node xform
* [x] Triangulate mesh
* [ ] Subdivision surface support(subdivide mesh using OpenSubdiv)
* [x] Resolve Material binding
  * [x] GeomSubset material binding
  * [ ] Collection material binding
* [x] Load and setup Texture
  * Colorspace conversion
    * [x] sRGB <-> Linear
    * [x] rec709 <-> Linear
    * [x] displayP3 Linear <-> sRGB Linear
    * [x] ACEScg(AP1, Linear) <-> sRGB Linear
    * [x] Rec.2020 <-> sRGB Linear
    * [x] Adobe RGB <-> sRGB Linear
    * [x] CIE XYZ-D65 <-> sRGB Linear
    * [x] Gamma 2.2 / 1.8 -> Linear
* [x] Skinning (LBS and Dual Quaternion)
* [x] BlendShape
* [x] Animation(SkelAnimation) - joint TRS, blend shape weights
* [x] Xform animation(timesamples XformOps)
* [x] Lights (SphereLight, RectLight, DistantLight, DomeLight)

### MaterialX Support

* [x] OpenPBR Surface shader conversion
* [x] Autodesk Standard Surface shader conversion (via OpenPBR)
* [x] UsdPreviewSurface shader conversion
* [x] NodeGraph traversal with texture/normal/tangent extraction
* [x] MaterialXConfigAPI metadata support
* See [doc/materialx.md](../../doc/materialx.md) for full details

### Tangent Computation and Quantization

Multiple tangent-space computation methods:

| Method | Speed | Quality |
|--------|-------|---------|
| Lengyel | ~16 MTri/s | Good (1-5 deg from reference) |
| MikkTSpace | ~0.7 MTri/s | Reference |
| FastMikkTSpace | ~2.0 MTri/s | Identical to MikkTSpace |
| Hybrid | ~3.5 MTri/s | Identical to MikkTSpace |

GPU-friendly packed formats:

| Format | Bytes/vertex | Max Error |
|--------|-------------|-----------|
| INT_2_10_10_10_REV | 4 | 0.08 deg |
| SNorm8x4 | 4 | 0.34 deg |
| FP16x4 | 8 | 0.03 deg |

Normal quantization (INT_2_10_10_10_REV) also supported for 67% storage savings.

See [doc/tydra-tangent.md](../../doc/tydra-tangent.md) for details.

### Memory Estimation

* [x] `RenderMesh::estimate_memory_usage()` - points, indices, normals, tangents, texcoords, joint data, blend shapes
* [x] `RenderScene::estimate_memory_usage()` - meshes, textures, nodes, materials, animations, skeletons

### Configuration (`MeshConverterConfig`)

| Option | Default | Effect |
|--------|---------|--------|
| `tangent_method` | FastMikkTSpace | Tangent computation algorithm |
| `tangent_storage` | Packed1010102 (WASM) / PackedFp16 (native) | Tangent quantization format |
| `normal_storage` | Packed1010102 (WASM) / Float3 (native) | Normal quantization format |
| `defer_tangent_computation` | false (true in WASM) | Defer tangent work to reduce initial load |
| `lowmem` | false (true in WASM) | Free source GeomMesh after conversion |
| `preserve_texel_bitdepth` | false (true in WASM) | Keep uint8 textures, avoid float32 bloat |

## Key Source Files

| File | Purpose |
|------|---------|
| `render-data.hh` | RenderScene, RenderMesh, RenderMaterial, MeshConverterConfig |
| `render-data.cc` | ConvertToRenderScene, tangent computation, memory estimation |
| `scene-access.hh` | Scene traversal and query APIs |
| `texture-util.hh` | Texture loading and colorspace conversion |
| `fast-mikktspace.hh` | FastMikkTSpace and Hybrid tangent implementations |
| `tangent-quantize.hh` | Packed tangent/normal formats for GPU |
| `materialx-to-json.hh` | MaterialX to JSON conversion |

## Documentation

* [doc/materialx.md](../../doc/materialx.md) - MaterialX pipeline details
* [doc/skinning.md](../../doc/skinning.md) - UsdSkel skinning equations and Tydra export
* [doc/tydra-tangent.md](../../doc/tydra-tangent.md) - Tangent computation algorithms and quantization
* [doc/threejs.md](../../doc/threejs.md) - Three.js animation and material integration
* [doc/memory-and-performance.md](../../doc/memory-and-performance.md) - Memory profiling and optimization

## TODO

- Data structure suited for realtime DCC.
- Data structure suited for Ray tracing
- ValueClip support (clip scheduling, time remapping)
- Material parameter timeSamples animation
- Vertex animation (points timeSamples)
- Camera/Light parameter timeSamples animation

EoL.
