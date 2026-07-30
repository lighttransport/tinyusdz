# tusdquicklook

A minimal, portable USD Quick Look viewer: point it at a folder, click a
`.usd*` file, get a shaded preview.

Where `examples/tusdview` is the full-featured viewer (GL + Vulkan raster,
Vulkan/CUDA/HIP ray tracing, ImGui, GLFW, gigabyte scenes), this is the opposite
end of the scale. It answers "what is in this file?" It runs with **no GPU**, it
holds a **bounded memory budget**, and it degrades — rather than dies — on files
too large to preview.

```
cmake -B build -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON \
               -DTINYUSDZ_BUILD_QUICKLOOK=ON
cmake --build build --target tusdquicklook -j16

./build/tusdquicklook ~/assets            # browse a folder
./build/tusdquicklook model.usdz          # open one file
./build/tusdquicklook model.usdz --screenshot out.png   # headless
```

`TINYUSDZ_BUILD_QUICKLOOK` is independent of `TINYUSDZ_BUILD_GUI_VIEWER`: this
target shares no code with tusdview and pulls in no window-system or GPU
dependency.

## What it is built on

| Piece | Why |
| --- | --- |
| **lightui** (`examples/common/lightui`) | Software-rendered C99 UI. Its X11 backend `dlopen`s libX11 with its own minimal ABI declarations, so there is no build-time or link-time X dependency — only `libdl`. That property is what makes the app portable and minimal; preserve it. |
| **`src/next`** (`next::StageSession`) | Lazy USD loading with progress/preview callbacks that can **abort mid-composition**. The budget is enforced from inside those callbacks, so a stage that would bust the cap is never fully built first. |
| **`src/tydra/next`** (`ConvertToSink`) | Streaming conversion keeps one geometry prim live at a time, and `SelectGeometry` lets us decide `Full`/`Proxy`/`Skip` **per mesh** against the running budget. |
| **lightrt** (`src/external/lightrt`) | CPU BVH kernel. Uses the tinyusdz-local `lrt_tri_scene_build_indexed`, which keeps indexed data instead of materializing a vertex soup. |
| **`src/tydra/next/mem-budget.hh`** | Tracking allocator + process cap, shared with `tools/tusdrender`. |

## Memory budget

`--max-mem <MB>` (default **512**), clamped at startup against what the machine
can actually back. It is split into advisory shares — stage/compose 55%,
geometry 25%, textures 10%, framebuffers + BVH 10% — over a single hard cap that
counts every `PoolAlloc` byte. Allocations past the cap throw `std::bad_alloc`
on the worker, which the loader catches and turns into a degraded preview.

Degradation ladder, each rung announced in the status bar:

1. Full composed preview with textures.
2. Textures dropped (count or byte cap reached) → flat base colours.
3. Proxy/bbox geometry for meshes that would not fit.
4. Uncomposed root layer, when composition itself busts the cap.
5. Skipped, with a "too large for quick look (projected X, budget Y)" card.

Rung 5 also has a pre-open form: file size × a per-format expansion factor gives
a projection *before* anything is allocated, so hopeless files never open. That
is what colours a row amber in the file list.

Measured: 54 MB peak RSS at `--max-mem 128` on a textured USDZ; the ctest
asserts it.

## Renderers

**CPU ray tracer** (default, always available). One BLAS per mesh plus a TLAS
with identity transforms — positions are baked to world space by the loader, so
the TLAS exists to key a hit back to its mesh, and geometry arriving during a
progressive load only costs a TLAS rebuild. Progression is an eighth-resolution
pass first (the "first pixel"), then full-resolution 32×32 tiles accumulating
one sample at a time; any camera or scene change resets the accumulator.
Shading is direct lighting only: Lambert + GGX from `UsdPreviewSurface` /
OpenPBR, base-colour texture, shadow rays, and a camera-locked three-point rig
when the stage authors no lights (which is most asset files).

**GL raster** (optional, `--backend gl`, or `auto` when the budget allows).
Deliberately **offscreen**: lightui has no GL window path and exposes no native
handle, so instead of patching the vendored UI, this creates a headless context
(EGL surfaceless / WGL / CGL, all `dlopen`ed at runtime), renders into an FBO and
reads back into the same blit-ready buffer. One UI code path, zero changes to
lightui, and it works with no compositor. The fragment shader mirrors
`render/shade.cc` — the ctest asserts the two backends agree within 10% of the
viewport; they currently differ by ~1%.

Note `auto` stays on the CPU below a 256 MB budget: a GL driver costs ~100 MB of
RSS that the budget cannot track, which under a tight cap dwarfs the preview.

## Known limits (v1)

- **No shadow maps in the GL path.** That is the main visible difference from
  the CPU renderer.
- **Meshes only.** Points, curves and PointInstancers are skipped. The
  `lrt_tlas` instancing needed for PointInstancer prototypes is already in place,
  so this is wiring, not architecture.
- **No animation, skinning or blend shapes.** One time sample.
- **Base-colour textures only**, downsampled to ≤512² at decode time. No normal,
  roughness or metallic maps.
- **No thumbnails in the file list.** The list shows names and sizes.
- **macOS is untested.** The Cocoa platform backend and the CGL context are
  wired and compiled, but nobody has run them.

## Testing

```
ctest -R tusdquicklook --output-on-failure
```

`tests/run-quicklook-smoke.sh` runs entirely headless (no display, no GPU) and
checks: geometry actually draws (each frame is compared against an empty-folder
render — a colour histogram would not catch a blank viewport, since the gradient
alone yields hundreds of colours), the budget bounds real RSS, `--threads 1` and
`--threads 8` are pixel-identical, the three `--backend` modes all produce an
image, cpu and gl agree, and a missing file exits non-zero with a diagnostic.

`tests/run-quicklook-gui-smoke.sh` drives the **real GUI** under Xvfb, because
the headless path never touches `lui_window_create`, the event loop or
`lui_window_present`. Both bugs found in that area during development were
invisible to `--screenshot`:

- `lui_window_create` hung forever with no window manager (upstream lightui
  waited for a `ConfigureNotify` that a bare X server never sends). Patched in
  the vendored copy — see `examples/common/lightui/README.tinyusdz.md`.
- the event loop blocked in `wait_event` before painting its first frame, so
  the window stayed blank until the user moved the mouse.

It also asserts the app actually goes idle when converged (0 CPU ticks over 3s
in practice), which is the "less cpu" half of the brief.

Two things worth knowing if you extend the tests:

- Compare the **viewport only** (`tests/pngdiff.py` does). The status bar prints
  live process RSS, so whole-image comparisons are flaky.
- Use a window at least ~480px wide. The file-list pane has a 160px minimum, so
  a small window leaves almost no 3D area to compare.

`tests/build-and-run-windows.sh` cross-builds with llvm-mingw (or system
mingw-w64) and smoke-tests the result under Wine. It skips cleanly when no cross
toolchain is present, and distinguishes "Wine cannot run any Windows binary
here" from "our binary failed" — an incomplete Wine install fails every `.exe`
identically, which would otherwise look like our bug.

**Status of the Windows port: verified.** The cross-build produces a PE32+
x86-64 `tusdquicklook.exe` (Win32 platform backend, WGL context, CPU tracer),
and running it under Wine loads a USD file, renders and writes a PNG identical
in content to the Linux build — Windows path handling included.

Two things the script handles automatically for the GCC path, both worth knowing
if you build by hand:

- `-Wno-error=maybe-uninitialized`, for a false positive in `src/usdMtlx.cc`
  (pre-existing library code, not this app; the library builds with `-Werror`).
- `-static-libgcc -static-libstdc++ -static`. Without it the `.exe` imports
  `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll` and will not
  start unless those sit beside it. A self-contained binary is what "portable"
  should mean here.

**macOS** is written but entirely untested: `platform_cocoa.m` is wired into the
lightui build and `render/gl_context_egl.cc` carries a CGL path, but nobody has
compiled or run either.
