# tusdview

An interactive USD viewer example for tinyusdz, rendering with **OpenGL 3.3** and
**Vulkan** behind a common backend interface. It loads a USD file
(`.usd/.usda/.usdc/.usdz`), converts it with the Tydra `RenderScene` API and
displays it with an ImGui docking UI.

## Features

- **Dockable Unity-like layout** (Dear ImGui docking branch):
  - **Hierarchy** browser — the Stage prim tree (name + type), with an optional
    RenderScene-node view.
  - **Inspector** — a 2-column table of the selected prim's properties/values
    (via `tydra::GetPropertyNames` / `GetProperty`).
  - **Viewport** — the 3D scene rendered to an offscreen target and shown as an
    `ImGui::Image`.
  - **Stats** — fps, mesh/triangle/material/texture counts, scene bounds, and a
    list of anything skipped (UDIM/undecoded textures, empty meshes).
  - **Search boxes** in the Hierarchy (filters prims by name/type/path, keeping
    matches' ancestors and auto-expanding), the Inspector (filters properties by
    name/value), and the Stage metadata panel (filters by key/value).
- **Maya-style navigation** (only when the viewport is hovered):
  - `Alt + LMB` orbit, `Alt + MMB` pan, `Alt + RMB` / **mouse wheel** dolly.
  - `F` frames (fits) the whole scene.
- **Base-color textures** on both backends (UsdPreviewSurface diffuse maps; GL
  also does metal/rough + emissive maps).
- **Three render techniques** — OpenGL raster, **Vulkan raster** (baseline), and
  **Vulkan ray tracing (ray query)**. RT is enabled only when the GPU exposes
  `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (+ `bufferDeviceAddress`)
  and the project was built with an RT-capable `glslangValidator`; otherwise the
  viewer transparently falls back to Vulkan rasterization. The RT path builds a
  BLAS per mesh + a TLAS, then a compute shader (`vk/shaders/raytrace.comp`)
  traces primary + shadow rays with `rayQueryEXT` into a storage image that is
  copied into the displayed target. Shading mirrors the raster look (material
  base color + the same directional light) plus **hard shadows**. Toggle it from
  **View ▸ Ray tracing (Vulkan)** (greyed out when unsupported) or start in RT
  with `--rt`. RT uses a ray-tracer-friendly conversion (no single-index dedup;
  `build_vertex_indices=false`). _v1 RT limitations:_ base color only (no
  textures), one material per mesh, no helper-line overlay.
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
  size, not `FontGlobalScale`). Tune with `--ui-scale S`.
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

# Budget controls (also useful for scripting/CI):
./build/tusdview --max-tris 5000000 model.usdz     # cap geometry (triangles)
./build/tusdview --time-budget 10 model.usdz        # abort conversion after 10s

# Headless full-window screenshot (UI + viewport, GL backend):
./build/tusdview --frames 8 --window-shot ui.ppm model.usdz
```

Note: `--frames` loads synchronously (deterministic screenshots); interactive
runs load on the worker thread.

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
| `load_usd {path}` | load a USD file (async; poll `get_scene_info`) |
| `get_scene_info` / `get_scene_bbox` | filepath, mesh/triangle/material counts, up axis, world-space AABB |
| `get_focused_prim` | selected prim: path, vertex/triangle counts, bbox, world matrix, material (id/name/base color/metal/rough) |
| `set_focus {path}` | select a prim by absolute path; returns the same info |
| `viewport {op}` | `orbit`/`pan {dx,dy}`, `dolly {amount}`, `fit`, `set {target,yaw,pitch,distance}`; returns the camera state |
| `list_prims {max?}` | renderable mesh prim paths |

Example:

```bash
curl -s -XPOST localhost:8080/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"viewport","arguments":{"op":"fit"}}}'
```

Tool calls are marshalled onto the render thread (GL/VK/camera are
main-thread-only), so they take effect on the next frame. The server is built by
default (deps — `nlohmann/json` + civetweb — are vendored in `src/external`);
disable with `-DTUSDVIEW_ENABLE_MCP=OFF`. The HTTP transport is the minimal
"Streamable HTTP" subset (one JSON response per POST; no SSE).

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

GL contexts, Vulkan command submission, ImGui and GLFW event polling are all
single-thread-affine, so the render/UI loop stays on the main thread. The
expensive, freeze-prone work (USD parse + Tydra convert + DrawScene build) runs
on a worker thread; its result is uploaded to the GPU on the main thread once
ready. The worker only touches CPU data — no GL/VK calls off-thread.

## Known limitations

- The **Vulkan** backend applies the **base-color** texture only; the OpenGL
  backend additionally uses metal-rough and emissive maps. Normal mapping is not
  done on either backend yet.
- Single hard-coded directional light (USD lights are not consumed yet).
- Y-up scenes assumed for navigation; tangents/normal-mapping, animation,
  instancing transforms, viewport click-picking and multi-viewport are out of
  scope for this pass.
