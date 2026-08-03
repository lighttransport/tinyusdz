/*
 * lightui/font.h — Font API (thin wrapper over LightType)
 *
 * All font lifecycle, metrics, and measurement functions are direct
 * aliases to their ltt_* equivalents in LightType.  The canvas draw
 * functions are implemented in src/font_bridge.c, converting
 * lvg_canvas_t to ltt_target_t.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_FONT_H
#define LIGHTUI_FONT_H

#include <lighttype/font.h>
#include <lightvg/canvas.h>
#include "frame_clock.h"
#include <lightvg/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Type aliases ------------------------------------------------------- */

typedef ltt_font_t          lui_font_t;
typedef ltt_font_backend_t  lui_font_backend_t;
typedef ltt_text_draw_state_t lui_text_draw_state_t;

#define LUI_FONT_BACKEND_CUSTOM   LTT_FONT_BACKEND_CUSTOM
#define LUI_FONT_BACKEND_FREETYPE LTT_FONT_BACKEND_FREETYPE

/* ---- Lifecycle / metrics / measurement (direct aliases) ----------------- */

#define lui_font_create        ltt_font_create
#define lui_font_create_from_memory ltt_font_create_from_memory
#define lui_font_destroy       ltt_font_destroy
#define lui_font_set_backend   ltt_font_set_backend
#define lui_font_get_backend   ltt_font_get_backend
#define lui_font_has_freetype  ltt_font_has_freetype
#define lui_font_ascent        ltt_font_ascent
#define lui_font_descent       ltt_font_descent
#define lui_font_line_height   ltt_font_line_height
#define lui_font_measure_text  ltt_font_measure_text
#define lui_text_draw_state_reset ltt_text_draw_state_reset

/* ---- Canvas draw functions (implemented in font_bridge.c) --------------- */

void lui_canvas_draw_text(lvg_canvas_t *canvas,
                           int x, int y,
                           const char *utf8, int len,
                           lui_font_t *font,
                           lvg_color_t color);

void lui_canvas_draw_text_spaced(lvg_canvas_t *canvas,
                                  int x, int y,
                                  const char *utf8, int len,
                                  lui_font_t *font,
                                  lvg_color_t color,
                                  int letter_spacing);

int lui_canvas_draw_text_partial(
    lvg_canvas_t *canvas, int x, int y,
    const char *utf8, int len,
    lui_font_t *font, lvg_color_t color,
    lui_text_draw_state_t *state,
    lui_frame_clock_t *clk);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_FONT_H */
