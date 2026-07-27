# tusdview

An interactive USD viewer example for tinyusdz, rendering with **OpenGL 3.3** and
**Vulkan** behind a common backend interface. It loads a USD file
(`.usd/.usda/.usdc/.usdz`), converts it with the Tydra `RenderScene` API and
displays it with an ImGui docking UI.

## Features

- **USD composition with lazy payloads** — `.usd/.usda/.usdc` files are composed
  on load (subLayers / references / payload / inherits / variants / specializes,
  LIVRPS order). `payload` arcs are **deferred by default** in interactive runs:
  the scene opens instantly showing non-payload content and the **Payloads**
  panel lists deferred payloads with per-prim **Load** buttons (plus
  **File ▸ Load All Payloads**); loading recomposes asynchronously on the worker
  thread. Headless/`--frames` runs load payloads eagerly so screenshots are
  complete. `--load-payloads` / `--defer-payloads` override either default;
  `--no-composition` loads the root layer only (legacy fast path, also used for
  `.usdz` archives for now). `--defer-references` extends the same lazy machinery
  to `references` arcs (the **Payloads** panel's **Arc** column shows
  `payload`/`reference`); it is **opt-in only and non-standard** — USD assumes
  references always resolve, so most scene content stays unloaded until you
  request it. Deferred references load on demand exactly like payloads (one
  whitelist drives both: loading a prim resolves its payloads and references).

- **Animation playback (Timeline panel)** — a **Timeline** panel docked along the
  bottom of the viewport plays time-sampled scenes. It reads `startTimeCode` /
  `endTimeCode` / `timeCodesPerSecond` from the stage and offers **Play/Pause**,
  **Stop** (reset to start), a frame **scrubber**, **Loop** and **Speed**. The
  viewer owns the playback clock; each new time re-evaluates the scene
  **asynchronously** (Tydra at `env.timecode`) on the worker thread and the
  request is **coalesced** (only one re-eval in flight — playback runs as fast as
  conversion sustains and never blocks the UI). Re-evaluation skips texture decode
  (`load_texture_assets=false`) and reuses the initial load's materials/textures,
  so only geometry/transforms are rebuilt. This animates **time-sampled
  transforms** (`xformOp` timeSamples), **deforming meshes** (time-sampled
  `points`), **value clips**, and **skeletal + blendshape animation**.
  Skinning (`skinning.cc`) runs on the **GPU by default** (`--skinning
  auto|cpu|gpu`): the rest mesh is uploaded once with per-vertex joint
  indices/weights, and each posed frame the viewer computes the skinning
  matrices (`inverse(bind)·posedWorld`, joint poses from the animation /
  blendshape weights from the stage's `SkelAnimation`, which Tydra does not
  emit), uploads them as an RGBA32F **bone-matrix texture**, and the vertex
  shader does linear-blend skinning. **Blendshapes** are applied in the **GPU
  vertex shader** too (before skinning): each target's primary + in-between
  samples become sparse per-vertex "channels", and a weight change uploads only a
  tiny per-channel coefficient array — no VBO re-upload. Deltas are stored
  half-precision; the shader skips the wide fetch for inactive channels (facial
  rigs: only a handful of 64+ targets active per frame). With displacement the
  morph + skin also run in the **GPU-tessellation** vertex stage, so deformed
  meshes tessellate the posed surface. The ray-tracing backends keep a CPU morph
  (baked into the traced geometry). **Animated node transforms** (a moving SkelRoot /
  parent Xform) are posed on the GPU path too: the affected mesh world matrices
  are re-evaluated from the stage each frame (`BuildXformNodeFromStage`, no
  geometry re-pack). The **CPU** path (`SkinPointsLBS`, full re-pose + upload) is
  the fallback, used when the renderer lacks GPU skinning or for the
  **ray-tracing** technique (whose BLAS needs baked vertices). For a static frame
  at a specific time (e.g. a headless screenshot), use `--time CODE` (alias
  `--frame`), which bakes the posed geometry too.

- **Dockable Unity-like layout** (Dear ImGui docking branch):
  - **Hierarchy** browser — the Stage prim tree (name + type), with an optional
    RenderScene-node view. The selected prim path is shown at the top, and
    double-clicking a hierarchy item frames that prim/subtree in the viewport.
    Selected paths are also shown as clickable breadcrumbs so you can jump back
    to ancestor prims quickly.
  - **Inspector** — a 2-column table of the selected prim's properties/values
    (via `tydra::GetPropertyNames` / `GetProperty`).
  - **Camera** — live eye/target/yaw/pitch/distance readout, preset view
    buttons, bookmark save/load controls, and interactive navigation tuning.
  - **Viewport** — the 3D scene rendered to an offscreen target and shown as an
    `ImGui::Image`.
  - **Stats** — fps, mesh/triangle/material/texture counts, scene bounds, and a
    list of anything skipped (UDIM/undecoded textures, empty meshes).
  - **Search boxes** in the Hierarchy (filters prims by name/type/path, keeping
    matches' ancestors and auto-expanding), the Inspector (filters properties by
    name/value), and the Stage metadata panel (filters by key/value).
- **Maya-style navigation & selection** (only when the viewport is hovered):
  - `Alt + LMB` orbit, `Alt + MMB` pan, `Alt + RMB` / **mouse wheel** dolly.
  - `W` / `S` moves both camera eye and orbit pivot forward/backward for
    right-handed mouse navigation; `Up` / `Down` are aliases and `Shift`
    increases the step.
  - `V` cycles shaded, wireframe-only, and wireframe-with-shading views.
  - **Navigation HUD** — an in-viewport cheatsheet/status overlay is shown by
    default and can be toggled with `F1`.
  - **Click to pick** — a plain left click selects the nearest mesh under the
    cursor (world-space ray vs. per-mesh AABB then ray/triangle), updating the
    hierarchy highlight, inspector and selection bbox.
  - **Hierarchy sync** — viewport or MCP selection auto-expands and scrolls the
    hierarchy to the focused prim when possible.
  - **Selection stepping** — `[` / `]` move to the previous/next visible
    renderable selection, making scene review faster without clicking.
  - **Selection history** — `Alt+Left` / `Alt+Right` move backward and forward
    through previously focused prims.
  - `F` frames (fits) the **selected** mesh (falls back to the whole scene);
    `A` frames the whole scene.
  - **Preset views** — `0` home, `5` isometric, `1 / Shift+1` front/back,
    `3 / Shift+3` right/left, `7 / Shift+7` top/bottom.
  - **Camera bookmarks** — `Ctrl+1..3` recall a saved view and
    `Ctrl+Shift+1..3` save the current camera/selection to a bookmark slot.
  - **Hide family** (Maya 2016+): `H` toggle-hide the selection, `Ctrl+H` hide,
    `Shift+H` show, `Alt+H` isolate (hide everything else). **View ▸ Unhide all**
    restores. Visibility is viewer-side (raster paths; the RT path traces all
    meshes) and is not written back to the USD `visibility` attribute.
- **Skeleton (UsdSkel) display** — the joint hierarchy is drawn as world-space
  bone line segments (parent→child) plus a small cross per joint, as an **X-ray
  overlay** (depth-test disabled, so bones stay visible through solid geometry).
  Joint bind poses are placed by the skinned mesh's world matrix (so bones align
  with the mesh even across an up-axis conversion). Toggle from **View ▸ Skeleton**.
- **Material textures** on both raster backends: base color, metal/rough,
  normal, emissive, and separate scalar opacity masks. Opacity honors selected
  channels, scale/bias, UV transforms and UV sets; ordinary and UDIM masks are
  supported. Varying `primvars:displayOpacity` modulates raster and RT output.
- **Native Ptex materials** — rectangular, bounded atlases preserve texture-
  local face IDs and intrinsic face UVs. Base color, metal/rough, normal,
  emissive, opacity, occlusion, specular color, and coat weight/color/roughness
  are sampled on OpenGL, Vulkan raster, Vulkan ray-query, and CUDA/HIP paths;
  displacement is baked once into the traced/raster geometry. Atlas residency
  is capped by the configured GPU budget, with representative-face fallback
  and render-report counters for downsampling, truncation, and degradation.
- Advanced OpenPBR/MaterialX lobes that are not yet evaluated in real time
  remain preserved in the neutral material record and produce one
  path-qualified load diagnostic per material. The structured load summary
  reports `unsupported_lobes=N` for transmission, subsurface, sheen/fuzz,
  anisotropy, thin-film, dispersion, and volume instead of silently treating
  them as supported.
- **Authored USD lighting** on both raster backends: up to 16 supported direct
  lights are evaluated in stage order with diffuse/specular multipliers,
  shaping cones, and per-mesh `collection:lightLink` masks, alongside
  DomeLight IBL. DistantLight and finite sphere/point, rect, disk, and cylinder
  records share the same packed GL/Vulkan interface; finite area shapes are
  currently represented by their light position. GeometryLight, PortalLight,
  IES profiles, emissive-mesh sampling, and raster shadow maps remain explicit
  roadmap items. Native Points/Curves currently receive all packed direct
  lights because their light-link carrier mapping has not landed yet.
- **UsdGeomPoints and curves (`--next`)** — Points plus BasisCurves,
  HermiteCurves, and NurbsCurves retain evaluated widths, displayColor,
  displayOpacity, material binding, purpose, animation time, and transforms.
  OpenGL raster draws world-sized camera-facing point discs and tessellated
  curve ribbons; Vulkan ray query and CUDA/HIP trace width-aware solid
  octahedron/tube proxies. Vulkan raster drawing and viewport click-picking for
  these carriers remain roadmap items; mesh picking is unchanged.
- **Surface displacement** (`UsdPreviewSurface inputs:displacement` — constant or
  a height texture, honoring the `UsdUVTexture` `scale`/`bias`). The **raster**
  paths displace in the vertex shader (coarse, no extra geometry), with an opt-in
  **GPU-tessellation** path (GL 4.1 / Vulkan tessellation shaders) for adaptive
  sub-triangle detail; **View ▸ Displacement** toggles it and exposes live scale +
  max-tess sliders (both live on GL and Vulkan). The tessellation vertex stage
  applies blendshape morph **and** skinning, so morphed/skinned displaced meshes
  tessellate the deformed surface (not the rest pose). The **ray-tracing** backends
  (Vulkan ray query, CUDA) intersect real triangles, so the displacement is baked
  into the geometry before the BLAS/TLAS is built. Geometric normals are used on
  displaced surfaces so the new detail shades correctly.
- **Three render techniques** — OpenGL raster, **Vulkan raster** (baseline), and
  **Vulkan ray tracing (ray query)**. RT is enabled only when the GPU exposes
  `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (+ `bufferDeviceAddress`)
  and the project was built with an RT-capable `glslangValidator`; otherwise the
  viewer transparently falls back to Vulkan rasterization. The RT path builds a
  BLAS per mesh + a TLAS, then a compute shader (`vk/shaders/raytrace.comp`)
  traces primary + shadow rays with `rayQueryEXT` into a storage image that is
  copied into the displayed target. Shading evaluates the shared GGX/coat
  material constants, authored USD lights, DomeLight IBL, and per-light
  visibility rays. Toggle it from
  **View ▸ Ray tracing (Vulkan)** (greyed out when unsupported) or start in RT
  with `--rt`. RT uses a ray-tracer-friendly conversion (no single-index dedup;
  `build_vertex_indices=false`); displacement is baked into the traced geometry.
  RT samples the six semantic material textures, including separate opacity,
  UV1/transforms, compressed-only sources, and sparse UDIMs; full ordinary,
  compressed, and UDIM mip chains use ray-footprint/projected-triangle LOD with
  trilinear filtering. Alpha-mask texels are rejected during traversal. Varying
  displayOpacity and material constants are also supported, including per-face
  materials bound through GeomSubsets.
  Helper lines, grid/axes, skeletons, volumes, and selection highlights are
  composited over the traced image without recreating the renderer.
- **Responsive, non-freezing UI**: USD parse → Tydra convert → DrawScene build
  run on a **worker thread** with a live progress modal; the GPU upload happens
  on the render thread when the worker finishes. The window stays interactive
  while a large file loads.
- **mmap + streaming/progressive loading** for interactivity:
  - The USDC file is **memory-mapped** (`io::MMapFile` + `LoadUSDFromMemory`,
    mapping kept alive for the Stage); uncompressed arrays stay zero-copy.
  - The Tydra conversion runs through the **streaming API**
    (`RenderSceneConverter::ConvertToRenderSceneStreaming`): the `DrawScene` is
    built **incrementally as each element is produced** — mesh geometry is
    interleaved as each mesh converts, world placement is applied when the node
    hierarchy is built, and textures/materials are decoded on completion (see
    `mesh_build.cc: BuildDrawSceneStreaming`). It produces a `DrawScene` identical
    to the monolithic `ConvertToRenderScene` + `BuildDrawScene` path.
  - After the worker finishes, meshes are **streamed to the GPU progressively**
    (time-budgeted per frame) so geometry pops in incrementally, then **textures
    stream in lazily** (surfaces show base color until their texture lands).
    The render loop never stalls on one big upload. Headless `--frames`
    uploads synchronously for deterministic screenshots.
- **Interruptible / budgeted loading** so huge scenes can't freeze the app or
  thrash VRAM:
  - **Cancel** button in the loading modal (and an automatic conversion
    **time budget**) aborts mid-conversion.
  - **Triangle / vertex-memory budget** stops building once the cap is hit and
    marks the scene *truncated* (editable in the Stats > Render budget panel).
- **HiDPI-ready appearance**: Maya-like dark theme, **Cascadia Mono** font
  (embedded, `examples/common/cascadia_mono.h`), and a UI scale that defaults to
  **2×** for 4K panels (font 16 → 32 px, crisp — the scale is baked into the font
  size, not `FontGlobalScale`). Tune with `--ui-scale S`, or load `font_size`,
  `window_scale`, and `window_size` from a JSON config file.
- **Rendering foundation** copied from
  [light3d](https://github.com/lighttransport/light3d) into
  `examples/common/light3d` (math, camera, material incl. GLSL shaders, texture).
- Gracefully **skips** what the GPU path can't show directly (e.g. raw UDIM,
  undecoded textures) instead of failing.

## Build

GUI deps (OpenGL/GLFW, optionally Vulkan) are opt-in so headless CI is
unaffected:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON \
  -DTINYUSDZ_BUILD_GUI_VIEWER=ON
cmake --build build -j16 --target tusdview
```

- **Vulkan** is enabled automatically when `find_package(Vulkan)` succeeds (needs
  `glslangValidator` to compile the shaders). Force a GL-only build with
  `-DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=ON`.
- **Vulkan ray tracing** additionally needs a `glslangValidator` that supports
  `GL_EXT_ray_query` (≈ glslang ≥ 11 / a Vulkan SDK build). CMake probes the
  compiler at configure time and only enables RT if it can compile
  `vk/shaders/raytrace.comp`; otherwise it prints a notice and the Vulkan backend
  uses rasterization. If your system glslang is too old, build a local one once
  (Linux):

  ```bash
  examples/common/build-glslang.sh      # clones + installs to examples/common/glslang
  cmake -S . -B build                    # auto-detects it (see the configure log)
  ```

  CMake prefers, in order: `-DTUSDVIEW_GLSLANG=/path`, `examples/common/glslang`,
  then the system compiler.
- The **native file dialog** (File > Open) is enabled when GTK3 is available on
  Linux (always on Windows/macOS); otherwise pass a file on the command line.

## Run

```bash
./build/tusdview models/suzanne-pbr.usda
./build/tusdview --backend vk models/suzanne-pbr.usda
./build/tusdview --rt models/suzanne-pbr.usda          # Vulkan ray tracing (implies --backend vk)

# Headless smoke test / screenshot (renders N frames then exits):
./build/tusdview --frames 5 --screenshot out.ppm models/suzanne-pbr.usda
./build/tusdview --backend vk --rt --frames 8 --screenshot rt.ppm models/suzanne-pbr.usda

# HiDPI: 2x is the default; tune for your display:
./build/tusdview --ui-scale 1.5 model.usdz

# Load startup settings from JSON:
./build/tusdview --config /path/to/tusdview.json model.usdz

# Budget controls (also useful for scripting/CI):
./build/tusdview --max-tris 5000000 model.usdz     # cap geometry (triangles)
./build/tusdview --time-budget 10 model.usdz        # abort conversion after 10s

# USD composition / lazy payloads:
./build/tusdview --load-payloads scene.usda         # compose payloads eagerly
./build/tusdview --defer-payloads scene.usda        # lazy payloads (interactive default)
./build/tusdview --defer-references scene.usda       # also defer references (opt-in, non-standard)
./build/tusdview --no-composition scene.usda        # root layer only (no arcs)

# Animation: scrub/play interactively from the Timeline panel, or render a
# static frame at a specific USD time code (headless screenshot of frame 12):
./build/tusdview --frames 4 --time 12 --screenshot frame12.ppm anim.usda

# Headless full-window screenshot (UI + viewport, GL backend):
./build/tusdview --frames 8 --window-shot ui.ppm model.usdz
```

Note: `--frames` loads synchronously (deterministic screenshots); interactive
runs load on the worker thread.

### Startup config

tusdview can load a JSON startup config automatically from the platform default
location, or from `--config /path/to/config.json`.

- **Linux:** `$XDG_CONFIG_HOME/tusdview/config.json`, else `~/.config/tusdview/config.json`
- **macOS:** `~/Library/Application Support/tusdview/config.json`
- **Windows:** `%APPDATA%\\tusdview\\config.json` (falls back to `%LOCALAPPDATA%`)

Supported keys (snake_case and kebab-case are both accepted):

```json
{
  "font_size": 24,
  "window_scale": 1.25,
  "window_size": {
    "width": 1600,
    "height": 900
  },
  "navigation": {
    "orbit_sensitivity": 1.0,
    "pan_sensitivity": 1.0,
    "dolly_sensitivity": 1.0,
    "invert_dolly": false
  },
  "composition": true,
  "payload_policy": "defer"
}
```

- `font_size` sets the ImGui font size in pixels and scales widget metrics to
  match.
- `window_scale` scales the default `1280x800` window/composite size.
- `window_size` sets an explicit logical window size and overrides
  `window_scale`.
- `navigation.orbit_sensitivity`, `navigation.pan_sensitivity`, and
  `navigation.dolly_sensitivity` tune camera response.
- `navigation.invert_dolly` flips wheel / Alt+RMB dolly direction.
- `composition` enables/disables USD composition on load (default `true`;
  `--no-composition` overrides).
- `payload_policy` is `"defer"` (lazy, interactive default) or `"load"` (eager);
  `--defer-payloads` / `--load-payloads` override.
- `--ui-scale` still works and overrides the config-derived font/window scaling
  for that run.

### Headless / CI (no physical display)

**Truly windowless (Vulkan, no X server at all)** — `--headless` skips GLFW, the
surface and the swapchain entirely: it renders the frame (and the full ImGui UI)
into an offscreen image on the GPU and reads it back. Nothing touches a display,
so it runs on a bare host with `DISPLAY` unset.

```bash
# 3D viewport screenshot, no display:
./build/tusdview --headless --frames 8 --screenshot out.ppm model.usdz

# Full UI composite (Hierarchy / Viewport / Inspector / Stage), no display:
./build/tusdview --headless --frames 8 --window-shot ui.ppm model.usdz

# Ray tracing works headless too:
./build/tusdview --headless --rt --frames 8 --screenshot rt.ppm model.usdz
```

`--headless` is Vulkan-only (OpenGL needs a window/context) and implies
`--backend vk`; it needs `--frames N` (windowless runs are bounded by frame
count). The composite size comes from `window_size`, or from `1280×800 ×
window_scale` / `--ui-scale` when no explicit size is configured. Unlike the GL
`--window-shot`, the headless `--window-shot` captures the **full UI** (it reads
the Vulkan composite image, not a GL back buffer).

**xvfb-run (either backend, in-memory X server)** — for the OpenGL backend (or
the windowed Vulkan path) on a machine with no display, wrap the run in
`xvfb-run`, which spins up a virtual X server. The GPU still does the rendering;
only window-system integration goes through the virtual display.

```bash
# No X server at all (Vulkan) — preferred when available:
./build/tusdview --headless --frames 8 --screenshot out.ppm model.usdz

# OpenGL offscreen render on a headless host (uses the real GPU, no monitor):
xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build/tusdview --backend gl --frames 8 --screenshot out.ppm model.usdz

# Vulkan via xvfb also works (windowed path):
xvfb-run -a -s "-screen 0 1280x800x24" \
  ./build/tusdview --backend vk --frames 8 --screenshot out.ppm model.usdz
```

- **No-X option:** `--headless` (Vulkan, above) needs no X server at all — prefer
  it on bare hosts. Use `xvfb-run` for the **OpenGL** backend or when you need the
  windowed (swapchain) Vulkan path.
- Use a **24-bit** screen depth (`x24`); the `xvfb-run` default (8-bit) can't
  give GL/Vulkan a usable visual.
- The screenshot PPM is the **3D viewport** at the (clamped) window size — pass a
  smaller `--ui-scale` to make it deterministic regardless of the screen size.
- The GL `--window-shot` grabs the GL back buffer (full UI on the GL backend);
  for windowless Vulkan UI capture use `--headless --window-shot` (above).

## MCP server (drive the viewer from an LLM/agent)

tusdview embeds an **MCP (Model Context Protocol)** server so an LLM/agent can
inspect and control the running viewer. It speaks JSON-RPC 2.0 over two
transports:

```bash
./build/tusdview --mcp-http=8080 model.usdz   # HTTP: POST JSON-RPC to http://localhost:8080/mcp
./build/tusdview --mcp-stdio    model.usdz     # stdio: JSON-RPC on stdin/stdout (logs on stderr)
./build/tusdview --mcp          model.usdz     # both (HTTP on 8080)
```

Tools (`tools/list` for schemas):

| tool | does |
|---|---|
| `load_usd {path}` / `load_usd {usda}` | load a USD path or inline USDA text (async; poll `get_scene_info`) |
| `get_scene_info` / `get_scene_bbox` | loading state/path, scene generation, filepath, mesh/triangle/material counts, up axis, world-space AABB |
| `get_focused_prim` | selected prim: path, vertex/triangle counts, bbox, world matrix, material (id/name/base color/metal/rough) |
| `set_focus {path}` | select a prim by absolute path; returns the same info |
| `viewport {op}` | `orbit`/`pan {dx,dy}`, `dolly`/`forward`/`backward {amount}`, `fit`, `home`, `isometric`, `front`, `back`, `right`, `left`, `top`, `bottom`, `bookmark_save {slot}`, `bookmark_load {slot}`, `set {target,yaw,pitch,distance}`; returns the camera state |
| `list_prims {max?}` | renderable mesh prim paths |
| `load_payloads {paths?}` | load deferred USD payloads (and deferred references under `--defer-references`); omit `paths` = all; async, poll `get_scene_info`, which reports `deferred_payloads` (each with an `arc` field) |
| `timeline {op, time?}` | animation playback: `op` = `play`/`pause`/`stop`/`seek {time}`; async re-eval, poll `get_scene_info` (reports `has_animation`/`time`/`start_time`/`end_time`/`fps`/`playing`) |
| `render_settings {mode?, grid?}` | change resettable render mode/grid state between captures without restarting the viewer |
| `screenshot {path}` | capture the current viewport as PNG, JPEG, or PPM (selected by extension) |

Example:

```bash
curl -s -XPOST localhost:8080/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"viewport","arguments":{"op":"fit"}}}'
```

In addition to the viewer tools above, the server also exposes the **tinyusdz
library's own USD tools** (`stage_info`, `prim_list`, `prim_get`, `attr_get`,
`query_prims_by_type`, `search`, `run_script`, composition tools, …) — any tool
name that isn't a viewer tool is forwarded to `tydra::mcp::CallTool` and run
against a snapshot of the currently-loaded Stage. (Query tools see a per-load
snapshot; tool-side edits aren't reflected in the 3D viewport.)

Tool calls are marshalled onto the render thread (GL/VK/camera are
main-thread-only), so they take effect on the next frame. The server is built by
default (deps — `nlohmann/json` + civetweb — are vendored in `src/external`);
disable with `-DTUSDVIEW_ENABLE_MCP=OFF`. The HTTP transport is the minimal
"Streamable HTTP" subset (one JSON response per POST; no SSE).

For render regressions, `tests/tusdview/mcp_render_batch.py` consumes a JSON
manifest and keeps one tusdview process alive for all compatible cases. Each
case may supply `path` or inline `usda`, expected scene-info fields,
`render_settings`, a `viewport` operation, and a screenshot name. Backend,
loader, RT implementation, startup environment, and other process-wide options
belong in separate batches. The `tusdview-mcp-render-batch` CTest is the compact
reference: it replaces three inline scenes and captures three render modes from
one Vulkan process. `tusdview-mcp-render-batch-gl-window` runs the same cases
through one real GLFW/OpenGL window under Xvfb. Both record the window and
renderer generations and fail if either resource is recreated within a batch.
The back-face material regression uses the same mechanism to keep one window or
offscreen renderer alive across its four fixture/camera combinations for GL,
Vulkan raster, and Vulkan RT. CUDA/HIP remain isolated because those backends
own and write their test screenshot during process shutdown.

## Architecture

```
USD ──LoadUSDFromFile──▶ Stage ──RenderSceneConverter──▶ tydra::RenderScene
                                                              │
                                            mesh_build.cc: RenderScene ▶ DrawScene
                                            (interleaved verts, submeshes, world
                                             matrices, decoded RGBA8 textures, AABB)
                                                              │
                                  ┌───────────────────────────┴──────────────┐
                            GLRenderer (GL330 uniforms)        VulkanRenderer (push-constants)
                                  └───────────── offscreen target ────────────┘
                                                              │
                                            ImGui::Image in the "Viewport" dock
```

`renderer.hh` is the GL/Vulkan boundary; everything else (loader, mesh build,
camera, GUI, `DrawScene`) is backend-neutral.

## Threading model

By default the render/UI loop stays on the main thread. USD parse, composition,
Tydra conversion, and DrawScene construction run on a CPU worker. Builds made
with `TUSDVIEW_ENABLE_GL_THREAD=ON` may opt into `--threaded`, where a dedicated
render thread owns the GL context or Vulkan queue while GLFW events and ImGui
frame construction remain on the main thread.

## Known limitations

- Vulkan/CUDA/HIP ray tracing samples base color, metallic, roughness, normal,
  emissive, and opacity images through the shared semantic table. The RT
  software texture path uses trilinear footprint/projected-triangle mip LOD,
  without raster anisotropy.
- Vulkan raster uses four bound descriptor sets and runs on low-limit software
  devices as well as desktop GPUs.
- Alpha-blended ray-traced surfaces use a nearest-layer approximation.
- MaterialX image nodes beyond the extracted Preview/OpenPBR semantic slots,
  and multi-viewport UI, remain incomplete.
