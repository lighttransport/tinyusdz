# tusdview Stage 2: GL render-thread decouple — follow-up task

## Goal

Make the tusdview window/UI stay responsive while the GPU works: move 3D drawing +
ImGui compositing + present off the event/UI thread onto a dedicated **render
thread**, so a slow frame, vsync stall, or `rebuildTlas` no longer freezes input,
resize, or the UI. Opt-in behind a `--threaded` flag (default off; the verified
single-threaded path stays the default). **GL backend first**; VK + the RT "busy"
UX are later follow-ups.

This is Stage 2 of the threading work. **Stage 1 (per-instance cull offload to a
worker) is already shipped** (commit `8d3fe987` on branch `tusdview`). A full Stage 2
prototype was written and **reverted** because it hit an unresolved render bug that
needs an interactive display + a GL debugger — this doc captures what was learned so
the next attempt starts ~80% of the way in.

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

## THE UNRESOLVED BUG (start here)

With all the above, the threaded path **builds, does not crash, uploads the scene
(`meshCount==1`), and `renderFrame` runs with `glGetError()==0`** — yet the offscreen
FBO read back via `captureViewport` is **white** (~20 non-bg px), so
`--threaded --frames --screenshot models/suzanne.usdc` fails the pixel-compare vs the
single-threaded screenshot. Capturing before vs after `present` made no difference;
creating `fbo_` on the render thread (vs main) made no difference.

Hypotheses to investigate with **RenderDoc / apitrace** (needs a display):
- Does `renderFrame`'s `glBindFramebuffer(fbo_)` + `glClear` + draws actually land in
  `fbo_`'s color attachment *on the render thread*, or silently go to the default
  framebuffer? (Capture the render thread's GL stream and inspect `fbo_`'s texture.)
- Is `resizeViewport` (posted op) recreating `fbo_`/`colorTex_` between the draw and
  the readback within the same iteration, so the capture reads a fresh (undefined =
  white) texture? Check the op-drain vs renderFrame ordering and whether the FBO id
  the draw binds matches the one `captureViewport` reads (log `fbo_` in both).
- Is the FBO actually complete on the render thread
  (`glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE`) at draw
  time? Add an assert.
- Is a stale ImGui scissor (`GL_SCISSOR_TEST`) from the previous frame's
  `RenderDrawData` clipping `glClear`/draws on the render thread? Try
  `glDisable(GL_SCISSOR_TEST)` at the top of `renderFrame`.

The white-with-a-tiny-dark-corner look specifically suggests draws hitting a small
region of an otherwise-uncleared/undefined attachment — i.e. a viewport/scissor/FBO
mismatch, not missing geometry (the geometry *is* uploaded).

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
