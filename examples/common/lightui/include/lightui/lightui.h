/*
 * lightui/lightui.h — Master include / library lifecycle
 *
 * Include this single header to pull in the full public API.
 *
 * Design goals
 * ------------
 *  - Pure C (C99).  No C++ or 3D-graphics API required.
 *  - Low-latency, HiDPI-first software rendering.
 *  - Font-rendering ready (FreeType + HarfBuzz integration points).
 *  - Platform backends: macOS (Cocoa), Linux (X11, Wayland),
 *    Windows (Direct2D / GDI), WebAssembly (Canvas 2D).
 *  - Build systems: CMake, Meson, xmake.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_H
#define LIGHTUI_H

#include <lightvg/types.h>
#include <lightvg/surface.h>
#include <lightvg/canvas.h>
#include "button.h"
#include "layout.h"
#include "widget_cache.h"
#include "label.h"
#include "scroll.h"
#include "slider.h"
#include "checkbox.h"
#include "colorwheel.h"
#include "curves.h"
#include "timeline.h"
#include "nodegraph.h"
#include "text_input.h"
#include "combo.h"
#include "chat.h"
#include "treeview.h"
#include "table.h"
#include "plot.h"
#include "menu.h"
#include "histogram.h"
#include "toolbar.h"
#include "progress.h"
#include "splitter.h"
#include "viewport.h"
#include "console.h"
#include "dopesheet.h"
#include "tabs.h"
#include "heatmap.h"
#include "gradient.h"
#include "tooltip.h"
#include "minimap.h"
#include "spinner.h"
#include "toggle.h"
#include "radio.h"
#include "knob.h"
#include "numentry.h"
#include "dialog.h"
#include "toast.h"
#include "statusbar.h"
#include "accordion.h"
#include "breadcrumb.h"
#include "palette.h"
#include "draglist.h"
#include "badge.h"
#include "propgrid.h"
#include "layerstack.h"
#include "rangeslider.h"
#include "gauge.h"
#include "filebrowser.h"
#include "taginput.h"
#include "searchbar.h"
#include "popover.h"
#include "ruler.h"
#include "stepper.h"
#include "rating.h"
#include "imagecrop.h"
#include "waveform.h"
#include "pagination.h"
#include "card.h"
#include "sparkline.h"
#include "markdown.h"
#include "text_edit.h"
#include "html.h"
#include "export.h"
#include "image.h"
#include "theme.h"
#include "event.h"
#include "window.h"
#include "mcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Version ------------------------------------------------------------ */

#define LIGHTUI_VERSION_MAJOR  0
#define LIGHTUI_VERSION_MINOR  2
#define LIGHTUI_VERSION_PATCH  0
#define LIGHTUI_VERSION_STRING "0.2.0"

/* ---- Lifecycle ---------------------------------------------------------- */

/**
 * Initialise the lightui library and the underlying platform backend.
 * Must be called once, from the main thread, before any other lui_* function.
 *
 * @return true on success, false on failure.
 */
bool lui_init(void);

/**
 * Shut down the library and release all OS-level resources.
 * All windows must have been destroyed before calling this function.
 */
void lui_shutdown(void);

/** Return the library version string (e.g. "0.2.0"). */
const char *lui_version(void);

/* Logger API (declarations live in <lightui/log.h> so library-internal
 * sources can pick them up without the full umbrella). */
#include "log.h"

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_H */
