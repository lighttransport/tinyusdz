# lightusd-local build for the vendored lightui subset.
#
# This is NOT upstream lightui's CMakeLists.txt. Upstream unconditionally builds
# ~25 example executables and does find_package(Python3 REQUIRED) for font
# embedding; we want exactly one static library and no Python at build time.
# See README.lightusd.md.
#
# Produces a single target `lightui` that folds in lightvg + lighttype.

if(TARGET lightui)
  return()
endif()

set(LIGHTUI_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ---- lightvg (2D software raster core) --------------------------------------
set(LIGHTUI_SOURCES
    lightvg/src/surface.c
    lightvg/src/canvas.c
    lightvg/src/backend_registry.c
    lightvg/src/dirty.c
    lightvg/src/layer.c
    lightvg/src/scene.c
)

# ---- lighttype (TTF parse + glyph raster, no external deps) -----------------
list(APPEND LIGHTUI_SOURCES
    lighttype/src/ttf_parse.c
    lighttype/src/rasterize.c
    lighttype/src/font.c
    src/font_bridge.c
)

# ---- lightui core ------------------------------------------------------------
list(APPEND LIGHTUI_SOURCES
    src/internal/lui_log.c
    src/lightui.c
    src/frame_clock.c
    src/layout.c
    src/widget_cache.c
    src/theme.c
    src/text_layout.c
    src/image.c            # pulls in third_party/wuffs-v0.4.c
)

# ---- widgets lusdquicklook uses ---------------------------------------------
# Kept deliberately short. Adding a widget = add its .c here; the headers for
# every upstream widget are present under include/lightui/ regardless.
list(APPEND LIGHTUI_SOURCES
    src/label.c
    src/button.c
    src/checkbox.c
    src/combo.c
    src/slider.c
    src/scroll.c
    src/treeview.c
    src/toolbar.c
    src/breadcrumb.c
    src/statusbar.c
    src/progress.c
    src/tabs.c
    src/splitter.c
    src/viewport.c
    src/tooltip.c
    src/toast.c
    src/dialog.c
    src/filebrowser.c
    src/text_input.c
)

# ---- embedded font -----------------------------------------------------------
# Pre-generated offline with upstream scripts/embed_binary_resource.py so the
# build has no Python dependency. Regenerate with:
#   python3 <lightui>/scripts/embed_binary_resource.py \
#       --input fonts/Hack-Regular.ttf --out-dir fonts \
#       --base hack_regular_ttf --api lui_embedded_font --mode array
list(APPEND LIGHTUI_SOURCES fonts/hack_regular_ttf.c)

# ---- platform backend --------------------------------------------------------
set(LIGHTUI_PLATFORM_DEFINES "")
set(LIGHTUI_PLATFORM_LIBS "")

if(WIN32)
  list(APPEND LIGHTUI_SOURCES src/platform/win32/platform_win32.c)
  list(APPEND LIGHTUI_PLATFORM_DEFINES LUI_PLATFORM_WIN32)
elseif(APPLE)
  enable_language(OBJC)
  list(APPEND LIGHTUI_SOURCES src/platform/cocoa/platform_cocoa.m)
  list(APPEND LIGHTUI_PLATFORM_DEFINES LUI_PLATFORM_COCOA)
  list(APPEND LIGHTUI_PLATFORM_LIBS "-framework Cocoa" "-framework CoreGraphics")
else()
  # X11. platform_x11.c carries its own minimal Xlib ABI declarations and
  # dlopen()s libX11 at runtime, so there is no build-time or link-time X
  # dependency -- only libdl.
  list(APPEND LIGHTUI_SOURCES src/platform/x11/platform_x11.c)
  list(APPEND LIGHTUI_PLATFORM_DEFINES LUI_PLATFORM_X11)
  list(APPEND LIGHTUI_PLATFORM_LIBS ${CMAKE_DL_LIBS})
endif()

add_library(lightui STATIC ${LIGHTUI_SOURCES})

set_target_properties(lightui PROPERTIES
    C_STANDARD 99
    C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    FOLDER "examples/common")

target_include_directories(lightui
    PUBLIC
        ${LIGHTUI_DIR}/include
        ${LIGHTUI_DIR}/lightvg/include
        ${LIGHTUI_DIR}/lighttype/include
        ${LIGHTUI_DIR}/fonts
    PRIVATE
        ${LIGHTUI_DIR}/src
        ${LIGHTUI_DIR}/lightvg/src
        ${LIGHTUI_DIR}/lighttype/src)

target_compile_definitions(lightui
    PRIVATE
        ${LIGHTUI_PLATFORM_DEFINES}
        LUI_HAVE_FONTS
    PUBLIC
        LVG_ENABLE_PNG
        LVG_ENABLE_JPEG)

# Optional SIMD for the software rasterizer. Scalar is the portable default;
# enable the arch-appropriate one since lusdquicklook blits every frame.
if(NOT DEFINED LVG_SIMD_DEF)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)")
    set(LVG_SIMD_DEF LVG_USE_SSE2=1)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64|ARM64)")
    set(LVG_SIMD_DEF LVG_USE_NEON=1)
  else()
    set(LVG_SIMD_DEF "")
  endif()
endif()
if(LVG_SIMD_DEF)
  target_compile_definitions(lightui PRIVATE ${LVG_SIMD_DEF})
endif()

target_link_libraries(lightui PUBLIC ${LIGHTUI_PLATFORM_LIBS})
if(UNIX)
  target_link_libraries(lightui PUBLIC m)
endif()

if(MSVC)
  target_compile_options(lightui PRIVATE /utf-8)
endif()

# The vendored C sources are upstream's; do not fail our build on their warnings.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(lightui PRIVATE -Wno-unused-function -Wno-unused-parameter)
endif()
