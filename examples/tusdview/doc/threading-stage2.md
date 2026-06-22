# tusdview Stage 2: GL render-thread decouple — follow-up task

> **Status (2026-06): LANDED for GL.** The render-thread path is implemented and
> verified byte-identical to the single-threaded path, gated behind the default-OFF
> CMake option `TUSDVIEW_ENABLE_GL_THREAD` + the `--threaded` flag. The
> previously-unresolved offscreen-render bug is fixed — see "THE OFFSCREEN-RENDER BUG
> — RESOLVED" below for the root cause. The VK render-thread port remains a follow-up.

## Goal

Make the tusdview window/UI stay responsive while the GPU works: move 3D drawing +
ImGui compositing + present off the event/UI thread onto a dedicated **render
thread**, so a slow frame, vsync stall, or `rebuildTlas` no longer freezes input,
resize, or the UI. Opt-in behind a `--threaded` flag (default off; the verified
single-threaded path stays the default). **GL backend first**; VK + the RT "busy"
UX are later follow-ups.

This is Stage 2 of the threading work. **Stage 1 (per-instance cull offload to a
worker) is already shipped** (commit `8d3fe987` on branch `tusdview`). The Stage 2 GL
render-thread path is now **implemented and verified** behind `TUSDVIEW_ENABLE_GL_THREAD`
(the prototype's unresolved render bug was a main-thread `resizeViewport` GL call —
fixed; see below). This doc remains the reference for the design and for the VK port.

## Why a render thread (and not the literal "imgui thread + draw thread")

A GL context is current on exactly one thread, and `ImGui_ImplOpenGL3_RenderDrawData`
is itself a GL call — so ImGui compositing, 3D draw, and `glfwSwapBuffers` MUST share
one thread. GLFW event polling + most `glfwGet*` are main-thread-only, and ImGui has
one global non-thread-safe context. So the only achievable split is:

- **Main/UI thread:** `glfwPollEvents` → `ImGui_ImplGlfw_NewFrame` → `ImGui::NewFrame`
  → `gui_.frame()` (UI build) → action handling → `ImGui::Render()` → deep-copy the
  draw data into a frame packet → hand off (never blocks on the GPU). Plus the
  existing load/reconvert workers.
- **Render thread:** owns the GL context; drains a GPU-op FIFO (uploads), then renders
  the latest packet (scene `renderFrame` → ImGui `RenderDrawData` → swap). The vsync +
  GPU stalls live here, off the UI thread.

## Confirmed-working pieces (from the reverted prototype)

These were validated to build + run; reuse them.

1. **Init/teardown ordering (this is the tricky part — get it right first):**
   - `ImGui_ImplGlfw_InitForOpenGL` (the ImGui *platform* backend) MUST run on the
     **main thread with the GL context current**. Calling it off-main, or with no
     context current, **segfaults inside `glfwSetWindowFocusCallback`** (GLFW lazily
     initializes per-thread state on first make-current). This was the first crash.
   - Working sequence:
     1. `initWindow` on main: `glfwInit` + create window + `glfwMakeContextCurrent` +
        GLAD load + `glfwSwapInterval(1)` (as today).
     2. main: `ImGui::CreateContext` + fonts + style + `renderer_->initImGuiPlatform()`
        (= `ImGui_ImplGlfw_InitForOpenGL`, context current).
     3. main: `glfwMakeContextCurrent(nullptr)` (release — a context is current on at
        most one thread).
     4. render thread: `glfwMakeContextCurrent(window)` → `renderer_->init()`
        (FBO/shaders) → `renderer_->initImGuiBackend()` (= `ImGui_ImplOpenGL3_Init`)
        → loop. Block the main thread on a "render-init-done" condvar/atomic.
     5. `~App`: `joinRenderThread()` (the render thread runs `renderer_->shutdown()` +
        `glfwMakeContextCurrent(nullptr)` before exit) BEFORE `ImGui::DestroyContext`
        + `glfwDestroyWindow`.
   - Split `GLRenderer::initImGui` into `initImGuiPlatform()` (ImplGlfw) +
     `initImGuiBackend()` (ImplOpenGL3); add `requiresRenderThreadInit()`.

2. **ImDrawData deep-copy** (mandatory — ImGui reuses its buffers next `NewFrame`):
   new `ImDrawData` with `Valid/CmdListsCount/Total*Count/DisplayPos/DisplaySize/
   FramebufferScale/OwnerViewport` copied, and each list via `ImDrawList::CloneOutput()`
   (confirmed present in the vendored ImGui 1.92.9, `imgui.h:3535`). Free on the render
   thread after `RenderDrawData` (`IM_DELETE` each list + the struct). This worked.

3. **`FramePacket`** owning copies of everything `renderFrame` needs (view/proj/
   cameraPos/mode/clearColor/depthScale/sceneMin/Extent/highlight*/helper+overlay
   lines/meshVisible/viewportW/H/fbW/fbH/`ImDrawData*`/seq) + a `params()` helper that
   returns a `RenderFrameParams` pointing into the packet's buffers. `gui.cc
   renderViewportScene(FramePacket*)`: when non-null, fill the packet + route GPU side-
   effects (resize, `updateInstanceVisibility`) through an injected `postGpu_`, and do
   NOT call `renderFrame`. `fbW/fbH` come from `glfwGetFramebufferSize` on **main**.

4. **GPU-op FIFO** (`std::mutex` + `std::queue<std::function<void()>>`): `App::postGpu`
   (queue when threaded, run inline otherwise) + `drainGpuOps` (render thread, FIFO,
   before each frame). Route the upload calls (`beginScene/uploadScene/appendMesh/
   uploadTexture/uploadSkinningFrame/updateMeshVertices/updateInstanceVisibility`)
   through it. Inject `gui_.setPostGpu([this](auto op){ postGpu(std::move(op)); })`.

5. **Upload-ordering hazard (handled):** the `--next` progressive path frees CPU
   geometry on the main thread right after the per-mesh upload → use-after-free when
   uploads are async. Workaround that worked: when threaded, force the one-shot
   `uploadScene` path and post the upload + the CPU-geometry free **together in one
   op** so they run in order on the render thread. (Progressive-threaded streaming is
   a separate follow-up.)

6. **Latest-wins handoff + capture handshake:** single-slot `pendingPacket_` (mutex +
   condvar); main overwrites the stale packet (freeing its draw data) and never waits
   in interactive mode (`maxFrames < 0`). In fixed-frame/capture mode (`maxFrames >=
   0`) the submit blocks until the render thread signals the packet's `seq` rendered,
   so `--threaded --frames --screenshot` is deterministic for pixel-compare. The
   render thread reads back the last frame into a buffer the `shot()` path then writes
   (since the GL context is on the render thread, not main).

7. **`presentThreaded(ImDrawData*, fbW, fbH)`** on the renderer: factor `GLRenderer::
   present()` into a `presentImpl(drawData, fbw, fbh)` shared by `present()` (live,
   `ImGui::GetDrawData()` + `glfwGetFramebufferSize`) and `presentThreaded` (packet
   draw data + packet fb size). `glfwGetFramebufferSize` is main-thread-only, so the
   render thread MUST use the packet's `fbW/fbH`.

## THE OFFSCREEN-RENDER BUG — RESOLVED (2026-06)

**Status: FIXED.** The whole Stage 2 GL render-thread path now produces a
**byte-identical** screenshot to the single-threaded path (`--threaded --backend gl
--frames 8 --screenshot` vs the non-threaded baseline, `maxdiff == 0` on `suzanne`
and `suzanne-pbr`). It is shipped gated behind the default-OFF CMake option
`TUSDVIEW_ENABLE_GL_THREAD` and the runtime `--threaded` flag.

### Root cause

`Gui::drawViewport()` (`gui.cc`) runs during the ImGui UI build on the **main
thread** and called `renderer_->resizeViewport(w, h)` **directly**. `resizeViewport`
is a GL op (it re-allocates the offscreen FBO's color texture + depth renderbuffer
via `ensureFbo`). On the threaded path the GL context is owned by the **render
thread**, so this `glTexImage2D`/`glRenderbufferStorage` ran on the main thread with
**no context current** and silently did nothing — leaving the color texture at its
first-frame size (64×20) while the FBO was reported `GL_FRAMEBUFFER_COMPLETE` at the
nominal viewport size (1469×1284). The render thread then cleared+drew into that
stale 64×20 texture, so the readback showed only `64*20 = 1280` cleared pixels in an
otherwise-black (undefined) attachment.

### How it was found (apitrace, as planned)

`apitrace trace` + `apitrace dump --thread-ids` showed the smoking gun: the resize
`glTexImage2D(width=1469, height=1284)` was tagged `@0` (main thread) while the
`glClear`/`glDrawElements` were `@1` (render thread). `glretrace` also emitted
`no current context` warnings around the resize calls. A temporary
`glCheckFramebufferStatus` + non-black-pixel count in `captureViewport` confirmed
`nonblack == 1280 == 64*20` with `fbstatus == GL_FRAMEBUFFER_COMPLETE, glerr == 0`.

### The fix

Route the `drawViewport()` resize through the GPU op-queue:
`gpu([this, w, h] { renderer_->resizeViewport(w, h); });` (inline on the
single-threaded path; queued to the render thread when threaded). `viewportTexture()`
returns a stable `colorTex_` id across resizes, so `ImGui::Image` can reference it
immediately and the render thread fills it. (`gui.cc renderViewportScene()` already
routed its own resize through `gpu()`; `drawViewport()` was the one direct call that
slipped through.)

### Lesson for the VK port

Audit **every** `renderer_->*` call reachable from `gui_.frame()` / the main-thread
UI build for GL/VK side-effects — any that touch the device must go through `gpu()`.
`resizeViewport` was the non-obvious one because it hides a GL allocation behind a
geometry-sized "resize".

## Implementation checklist (files)

- `frame_packet.{hh,cc}` (new): `FramePacket` + `CloneImDrawData`/`FreeImDrawData`.
  Add to `CMakeLists.txt` `TUSDVIEW_SOURCES`.
- `renderer.hh`: forward-declare `struct ImDrawData;`; add `presentThreaded`,
  `initImGuiPlatform`/`initImGuiBackend`/`requiresRenderThreadInit` virtuals.
- `gl/gl_renderer.{cc,hh}`: split `initImGui`; `presentImpl` + `presentThreaded`;
  `present` uses packet fb size in the threaded path.
- `app.hh/app.cc`: `--threaded` → `threaded_`; `renderThreadActive_ = threaded_ &&
  !headless_ && backend_==GL`; render-thread members (thread, mutex/condvar,
  `gpuOps_`, `pendingPacket_`, init/done atomics, capture buffer); `startRenderThread`/
  `joinRenderThread`/`renderThreadMain`/`postGpu`/`drainGpuOps`/`submitFramePacket`;
  init restructure (platform on main, init on render thread); loop branch (skip
  `renderer_->newFrame()` on main; build+submit packet instead of inline
  `renderFrame`+`present`); `applyLoaded` forces one-shot upload when threaded; `~App`
  joins the render thread; `shot()` uses the render-thread capture buffer when threaded.
- `gui.{cc,hh}`: `renderViewportScene(FramePacket* = nullptr)`; `setPostGpu` + a
  `gpu()` helper; route `resizeViewport` + both `updateInstanceVisibility` sites
  through `gpu()`.
- `main.cc`: `--threaded` flag → `app.setThreaded`.

## Verification

- **Pixel-compare (the gate):** `tusdview --threaded --backend gl --frames 6
  --screenshot /tmp/on.png models/suzanne.usdc` vs the same without `--threaded` →
  must be byte-identical (or within AA tolerance). Then a `--next` instanced scene
  (e.g. `/tmp/grid100.usda`) + a Caldera prefab.
- **Interactive smoke (you, at a display):** load a large scene; confirm UI/panels/
  timeline/orbit stay responsive while geometry/streaming/`rebuildTlas` happen;
  dock/window resize + minimize/restore; clean quit + quit mid-load with no hang and
  no GL validation errors (run with a GL debug context).
- Headless + non-`--threaded` runs must be unchanged (they keep the inline path).

## Notes / scope

- Keep the single-threaded path the default and untouched (it's the verified one).
- Headless (`--frames`/`--screenshot` without a window) stays single-threaded for
  deterministic screenshots — gate the render thread behind `renderThreadActive_`.
- Out of scope for this task: VK render-thread port; progressive-threaded streaming;
  VK-RT `rebuildTlas` async + "tracing…" HUD; window-shot in threaded mode.
