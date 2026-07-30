/*
 * src/font_bridge.c — Bridge between LightUI canvas and LightType font rendering
 *
 * Provides lui_canvas_draw_text() and friends by converting lvg_canvas_t
 * to ltt_target_t and dispatching to LightType.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/font.h>
#include <lightvg/canvas.h>
#include <lightui/frame_clock.h>
#include <lighttype/font.h>

/* Convert a LightUI canvas to a LightType render target. */
static ltt_target_t canvas_to_target(lvg_canvas_t *canvas)
{
    ltt_target_t t;
    t.pixels = canvas->_surface->pixels;
    t.width  = canvas->_surface->width;
    t.height = canvas->_surface->height;
    t.stride = canvas->_surface->stride;
    t.clip.x      = canvas->_clip.x;
    t.clip.y      = canvas->_clip.y;
    t.clip.width  = canvas->_clip.width;
    t.clip.height = canvas->_clip.height;
    return t;
}

void lui_canvas_draw_text(lvg_canvas_t *canvas,
                           int x, int y,
                           const char *utf8, int len,
                           lui_font_t *font,
                           lvg_color_t color)
{
    if (!canvas || !canvas->_surface || !canvas->_surface->pixels) return;
    /* Text rasterizes directly into the surface buffer. Flush any pending
     * backend shapes first so they land in the buffer before we composite
     * glyph pixels on top — otherwise a deferred backend (e.g. thorvg)
     * would overwrite the text on its next draw()/sync(). */
    lvg_canvas_flush(canvas);
    ltt_target_t t = canvas_to_target(canvas);
    ltt_draw_text(&t, x, y, utf8, len, font, color);
}

void lui_canvas_draw_text_spaced(lvg_canvas_t *canvas,
                                  int x, int y,
                                  const char *utf8, int len,
                                  lui_font_t *font,
                                  lvg_color_t color,
                                  int letter_spacing)
{
    if (!canvas || !canvas->_surface || !canvas->_surface->pixels) return;
    lvg_canvas_flush(canvas);
    ltt_target_t t = canvas_to_target(canvas);
    ltt_draw_text_spaced(&t, x, y, utf8, len, font, color, letter_spacing);
}

/* Deadline adapter: wraps lui_frame_clock_t for LightType's callback */
static int frame_clock_deadline(void *ctx)
{
    return ctx ? lui_frame_clock_expired((lui_frame_clock_t *)ctx) : 0;
}

int lui_canvas_draw_text_partial(
    lvg_canvas_t *canvas, int x, int y,
    const char *utf8, int len,
    lui_font_t *font, lvg_color_t color,
    lui_text_draw_state_t *state,
    lui_frame_clock_t *clk)
{
    if (!canvas || !canvas->_surface || !canvas->_surface->pixels) return 1;
    lvg_canvas_flush(canvas);
    ltt_target_t t = canvas_to_target(canvas);
    return ltt_draw_text_partial(&t, x, y, utf8, len, font, color,
                                  state, frame_clock_deadline, clk);
}
