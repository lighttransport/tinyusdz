# tusdview Stage 2: GL render-thread decouple — follow-up task

> **Status (2026-06): LANDED for GL + Vulkan rasterization. VK ray tracing is
> excluded.** The render-thread path is implemented and verified byte-identical to
> the single-threaded path for GL and Vulkan *rasterization*, gated behind the
> default-OFF CMake option `TUSDVIEW_ENABLE_GL_THREAD` + the `--threaded` flag. The
> previously-unresolved offscreen-render bug is fixed — see "THE OFFSCREEN-RENDER BUG
> — RESOLVED". VK port notes are in "VK render-thread port" near the end.
>
> **Known limitation — VK ray tracing (`--rt`) is NOT threaded.** Threaded VK-RT has
> an unresolved race: the offscreen RT trace + the render-thread screenshot capture
> intermittently read a blank frame (~40% of fixed-frame runs). Each path is
> internally deterministic (single≡single, threaded≡threaded) but threaded≠single,
> and capture-after-present + a present-retry did not resolve it (`colorImg_` is still
> intermittently unwritten — root cause not found). So `--threaded` + `--backend vk`
> + `--rt` transparently falls back to the single-threaded path with a `LOGW`
> (`app.cc renderThreadActive_`). Re-enabling needs the race root-caused first. A
> separate, also-unresolved issue: threaded GPU **blendshape** at load differs from
> single-threaded (the per-mesh morph upload path interacts badly with the threaded
> load ordering) — interactive skeletal/skinning through the op-queue is still a
> follow-up (see "Notes / scope").

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
- Out of scope for this task: progressive-threaded streaming; animation/GPU-skinning
  through the op-queue (still issued from the main thread); VK-RT `rebuildTlas` async
  + "tracing…" HUD; window-shot in threaded mode.

## VK render-thread port (LANDED 2026-06)

Vulkan is actually *more* thread-amenable than GL: there is no per-thread "current
context", so the only rule is **single-queue-owner** — every `vkQueueSubmit` /
`vkQueuePresentKHR` (present, the per-frame one-shot upload submits, and the
synchronous `rebuildTlas` AS builds) must come from one thread. The render thread
owns them; all uploads already route through the `gpu()` op-queue (drained on the
render thread), and the main thread issues no queue work.

What the port changed (all in `vk/vk_renderer.{cc,hh}` + a few `app.cc` guards):

- `present()` → thin wrapper over `presentImpl(ImDrawData*, fbW, fbH)`; the threaded
  `presentThreaded()` passes the packet's deep-copied draw data + main-thread fb size.
  The one `ImGui::GetDrawData()` use inside the swapchain pass now takes the param.
- `glfwGetFramebufferSize` is main-thread-only, so `recreateSwapchain()` and the
  `createSwapchain` `currentExtent==0xFFFFFFFF` fallback use `winFbW_/winFbH_` carried
  in by `presentImpl` (0 ⇒ single-threaded ⇒ GLFW query as before). On X11
  `currentExtent` is the real size, so init needs no GLFW query.
- `initImGui` split: `initImGuiPlatform(window)` = `ImGui_ImplGlfw_InitForVulkan` on
  the main thread (captures `window_`, same null-window-segfault fix as GL);
  `initImGuiBackend` = `ImGui_ImplVulkan_Init` on the render thread (needs
  device/queue/render pass from `init()`); shared body factored to
  `initVulkanImGuiBackend()`.
- `app.cc`: `renderThreadActive_` now also true for `Backend::Vulkan`;
  `renderThreadMain` skips the `glfwMakeContextCurrent` calls when not GL; `--rt` is
  activated after the render thread inits, via `postGpu(setRayTracing)` so the
  RT/`tlasDirty_` flags are written on the thread that reads them.

Verified byte-identical (`maxdiff 0`) threaded-vs-single on `suzanne.usdc` and
`suzanne-pbr.usda` for VK **rasterization**. VK **ray tracing** is excluded from the
threaded path (see the "Known limitation" note at the top): an earlier claim that
threaded VK-RT was byte-identical turned out to rest on a flaky run — re-testing shows
a ~40% blank-capture race, so `--rt` now falls back to single-threaded. (No validation
layer on the dev box; verification is pixel-identity.)
