// SPDX-License-Identifier: Apache-2.0
//
// tusdquicklook — theme, layout and small drawing helpers over lightui/lightvg.
//
// Everything here draws into the single lvg_surface_t owned by the window;
// there is no retained widget tree beyond what lightui's own widget structs
// provide.
#pragma once

#include <string>

extern "C" {
#include <lightui/font.h>
#include <lightvg/canvas.h>
#include <lightvg/surface.h>
#include <lightvg/types.h>
}

namespace tusdql {

struct Theme {
  lvg_color_t bg;
  lvg_color_t panel;
  lvg_color_t panel_alt;   // zebra striping in the file list
  lvg_color_t selection;
  lvg_color_t border;
  lvg_color_t text;
  lvg_color_t text_dim;
  lvg_color_t accent;
  lvg_color_t warn;
  lvg_color_t error;
  lvg_color_t viewport_top;
  lvg_color_t viewport_bottom;

  int pad;
  int row_h;
  int statusbar_h;
  int toolbar_h;
  int font_px;
};

const Theme& DefaultTheme();

// Region rects in surface (physical) coordinates.
struct Layout {
  lvg_rect_t toolbar;
  lvg_rect_t list;
  lvg_rect_t splitter;
  lvg_rect_t viewport;
  lvg_rect_t statusbar;
};

// `left_pane_w` is the list-pane width in surface pixels; it is clamped to a
// sane range so a dragged splitter can never make a pane unusable.
Layout ComputeLayout(const Theme& th, int surf_w, int surf_h, int left_pane_w);

int ClampPaneWidth(int requested, int surf_w);

// ---- drawing helpers --------------------------------------------------------

void FillRect(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t color);
void StrokeRect(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t color);

// Vertical two-stop gradient; used for the viewport background.
void FillRectVGradient(lvg_canvas_t* c, const lvg_rect_t& r, lvg_color_t top,
                       lvg_color_t bottom);

// Draw `text` with the baseline at `baseline_y`, truncating with an ellipsis
// so it never exceeds `max_w`. Returns the advance width actually drawn.
int DrawTextEllipsized(lvg_canvas_t* c, lui_font_t* font, int x, int baseline_y,
                       int max_w, const std::string& text, lvg_color_t color);

// Right-aligned variant (used by the status bar).
int DrawTextRight(lvg_canvas_t* c, lui_font_t* font, int right_x,
                  int baseline_y, const std::string& text, lvg_color_t color);

// Centered single line inside `r` (used for placeholder / error cards).
void DrawTextCentered(lvg_canvas_t* c, lui_font_t* font, const lvg_rect_t& r,
                      const std::string& text, lvg_color_t color);

// Word-wrapped, horizontally centered, vertically centered as a block. Wraps on
// spaces; the last line is ellipsized if the text needs more than `max_lines`.
// Used for viewport message cards, which carry numbers worth keeping.
void DrawTextWrappedCentered(lvg_canvas_t* c, lui_font_t* font,
                             const lvg_rect_t& r, const std::string& text,
                             lvg_color_t color, int max_lines);

// Human-readable byte count, e.g. "1.4 GB", "512 MB", "9.2 KB".
std::string FormatBytes(uint64_t bytes);

// Human-readable count with thousands separators, e.g. "1,234,567".
std::string FormatCount(uint64_t n);

}  // namespace tusdql
