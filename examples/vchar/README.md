# vchar

`vchar` is TinyUSDZ's native virtual-human rendering profile. It is emitted
beside `tusdview` when `TINYUSDZ_BUILD_GUI_VIEWER=ON` and deliberately uses the
same executable image: renderer, UsdSkel evaluation, blendshape deformation,
MaterialX/OpenPBR materials, texture cache, and BasisCurves hair stay identical.

```sh
./build_ninja/vchar --backend gl character.usdz
./build_ninja/vchar --backend vk character.usdz
./build_ninja/vchar --backend vk --rt character.usdz
./build_ninja/vchar --mcp-http 8765 character.usdz
```

The profile accepts USD, USDA, USDC and packaged USDZ input. Skeleton debug is
enabled initially. OpenGL and Vulkan raster are production paths; Vulkan ray
query is the native RT preview. CUDA and HIP are named capability slots but are
disabled in this profile until their virtual-human material/deformation parity
is validated.

## Facial-first control and debug API

The embedded MCP server adds:

- `vchar_status`
- `list_blendshapes`, `set_blendshape_weights`
- `list_facial_controls`, `set_facial_controls`
- `vchar_debug` (`skin-weights`, `blend-influence`, normals, tangents, skeleton)
- `autorigger_inspect`, `apply_rig_overlay`

Without authored metadata, named facial controls map directly to USD blendshape
names. A prim can instead author parallel arrays in `customData`:

```usda
customData = {
    dictionary vchar = {
        string[] controlNames = ["jawOpen"]
        string[] controlMappings = ["jaw_open"]
        float2[] controlRanges = [(0, 1)]
        float[] controlDefaults = [0]
    }
}
```

The first authored control map found in stage traversal order is authoritative.
Missing mapping/range/default entries fall back to the control name, `[-1,1]`,
and zero. The dedicated Facial Rig panel uses the authored labels, limits, and
defaults directly; hovering a control shows its mapped USD blendshape. UsdSkel
joint animation and blendshapes execute in realtime.

`vchar_physics` initializes and steps TinyUSDZ's bounded rigid-body solver from
authored UsdPhysics APIs. Dynamic/awake body positions are drawn as orange
crosses (sleeping bodies are dimmed). `status`, `initialize`, `step`, `reset`,
and `hide` are supported. With `--legacy-load`, each solver step writes body
translation/orientation back to its matching rigid-body prim and updates the
renderer's mesh-world buffers. The orange crosses remain visible for comparing
the rendered geometry against solver state. The next-core loader rejects
simulation explicitly until its physics annotation bridge can retain the same
body-to-prim mapping.

## External auto-rigger boundary

The external project is a persistent JSON-RPC 2.0 worker over stdin/stdout. Each
line is one UTF-8 JSON message; stdout must contain protocol messages only.
Supported methods are `rig.initialize`, `rig.inspect`, `rig.submit`,
`rig.status`, and `rig.cancel`. `rig.submit` receives the source asset path,
facial-first options, and an output-layer path. Its authoritative result is a
USDA or USDC overlay, never a rewritten character asset.

Compose the completed layer with `apply_rig_overlay`. vchar creates a temporary
session root whose sublayers are the overlay and original asset, then reloads
that root. This keeps the source USDZ untouched and makes auto-rig results easy
to replace or discard. See [autorigger-protocol.schema.json](autorigger-protocol.schema.json)
for the request/result shape.

Hair uses the shared BasisCurves ribbon rasterizer (and tube proxies for Vulkan
RT), including authored widths and curve tangents. Skin uses the shared
MaterialX/OpenPBR subsurface inputs; raster uses the viewer's realtime SSS
approximation while RT evaluates the same material data through LightRT.
