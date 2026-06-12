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
| `load_usd {path}` | load a USD file (async; poll `get_scene_info`) |
| `get_scene_info` / `get_scene_bbox` | filepath, mesh/triangle/material counts, up axis, world-space AABB |
| `get_focused_prim` | selected prim: path, vertex/triangle counts, bbox, world matrix, material (id/name/base color/metal/rough) |
| `set_focus {path}` | select a prim by absolute path; returns the same info |
| `viewport {op}` | `orbit`/`pan {dx,dy}`, `dolly {amount}`, `fit`, `home`, `isometric`, `front`, `back`, `right`, `left`, `top`, `bottom`, `bookmark_save {slot}`, `bookmark_load {slot}`, `set {target,yaw,pitch,distance}`; returns the camera state |
| `list_prims {max?}` | renderable mesh prim paths |
| `load_payloads {paths?}` | load deferred USD payloads (and deferred references under `--defer-references`); omit `paths` = all; async, poll `get_scene_info`, which reports `deferred_payloads` (each with an `arc` field) |

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
- Skeleton display shows the **bind pose** only (no animation/skinning playback).
- Tangents/normal-mapping, animation, instancing transforms and multi-viewport
  are out of scope for this pass.
