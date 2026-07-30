# lightui Vendor Import

Upstream: https://github.com/syoyo/lightui (local: `~/work/lightui`)

Vendored **subset** snapshot, not a git submodule. Imported for
`examples/tusdquicklook`, which needs a portable software-rendered UI without
ImGui/GLFW/OpenGL (unlike `examples/tusdview`).

## Snapshot

Upstream `main` @ `ace7db70518b71a7970d5374e77be85c6c03b865` (2026-07-23),
imported 2026-07-30.

tinyusdz-owned files:

- `CMakeLists.txt` — ours, replaces upstream's (see below)
- `README.tinyusdz.md` — this file
- `fonts/hack_regular_ttf.{c,h}` — pre-generated, see below

## tinyusdz-local patches (must survive a re-sync)

**`src/platform/x11/platform_x11.c` — `x11_sync_configured_size()` must not block.**

Upstream loops on `XNextEvent` until it observes a `ConfigureNotify`. That
assumes a reparenting window manager, which always reconfigures a newly mapped
window. With **no window manager** — Xvfb, a bare X session, a kiosk — the
window is created at exactly the requested size and is never reconfigured, so
no `ConfigureNotify` is ever sent and `lui_window_create()` hangs forever,
before the application can draw its first frame.

The patch replaces the blocking loop with a non-blocking
`XCheckTypedWindowEvent` drain after an `XFlush`. Behaviour is unchanged when a
WM is present; without one it returns immediately. Adopting the server's
configured size is an optimization, not a correctness requirement — the window
already has the requested size, and a later reconfigure still reaches the app
as an ordinary resize event (`StructureNotifyMask` is selected).

Worth reporting upstream. Verified with the interactive smoke test under Xvfb
(`examples/tusdquicklook/tests/run-quicklook-gui-smoke.sh`), which hangs without
the patch.

## What was vendored

Kept: `include/`, `lightvg/`, `lighttype/` (minus `font_freetype.c`),
`src/*.c` top-level widgets/core, `src/internal/`, `src/platform/{x11,win32,cocoa}/`,
`third_party/{wuffs-v0.4.c,stb_image_write.h}`, `fonts/Hack-Regular.ttf`.

Dropped: `src/mcp/` (LLM-debug HTTP server + socket deps), `src/markdown*`,
`src/html*`, `src/export_*`, `src/image_cmp.c`, `src/geomap/`, `src/svg/`,
`src/vg/backends/`, `lightvg/src/backends/` (blend2d/thorvg/skia),
`src/platform/{wayland,wasm}/`, `tui/`, `examples/`, `tests/`, `benchmarks/`,
`subprojects/`, `ref/`, `doc/`, meson/xmake build files, and all fonts except
Hack-Regular.

Headers for dropped widgets (e.g. `include/lightui/markdown.h`) are still
present since `include/lightui/lightui.h` includes them; the corresponding
`.c` files are absent, so referencing those widgets is a link error rather than
a compile error. Add the `.c` back if you need one.

## Our CMakeLists.txt

Upstream's top-level `CMakeLists.txt` is not usable via `add_subdirectory`: it
unconditionally builds ~25 example executables and does
`find_package(Python3 REQUIRED)` for font embedding. Ours builds a single static
`lightui` target folding in lightvg + lighttype, guarded by
`if(TARGET lightui) return() endif()`.

Notable: the X11 backend carries its own minimal Xlib ABI declarations
(`src/platform/x11/lui_x11.h`) and `dlopen`s `libX11` at runtime
(`lui_x11_loader.h`), so there is **no build-time or link-time X dependency** —
only `${CMAKE_DL_LIBS}`. That property is why lightui was chosen; preserve it.

## Embedded font

`fonts/hack_regular_ttf.{c,h}` are generated **offline** so the build needs no
Python. Regenerate after replacing the TTF:

```
python3 ~/work/lightui/scripts/embed_binary_resource.py \
    --input fonts/Hack-Regular.ttf --out-dir fonts \
    --base hack_regular_ttf --api lui_embedded_font --mode array
```

Consume with `ltt_font_create_from_memory(lui_embedded_font_data(), lui_embedded_font_size(), px)`.

## C linkage

lightui is C99. Include from C++ via the umbrella header; upstream headers
already guard with `#ifdef __cplusplus` / `extern "C"`.

## Licenses

`LICENSE` (lightui, upstream), `fonts/LICENSE.md` (Hack font),
`third_party/wuffs-v0.4.c` (Apache-2.0/MIT, header comment),
`third_party/stb_image_write.h` (public domain / MIT).
