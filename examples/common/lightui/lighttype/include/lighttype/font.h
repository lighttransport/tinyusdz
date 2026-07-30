/*
 * lighttype/font.h — CPU font shaping and rendering
 *
 * LightType: standalone font library with built-in TrueType/CFF parser,
 * scanline rasterizer, glyph cache, and optional FreeType/HarfBuzz backend.
 * No UI framework dependency — draws to a raw pixel buffer (ltt_target_t).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTTYPE_FONT_H
#define LIGHTTYPE_FONT_H

#include "types.h"

#include <stddef.h>  /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Font context ------------------------------------------------------- */

/** Opaque font context. */
typedef struct ltt_font_s ltt_font_t;

/** Font rendering backend. */
typedef enum {
    LTT_FONT_BACKEND_CUSTOM   = 0,  /**< Built-in TTF parser + rasterizer */
    LTT_FONT_BACKEND_FREETYPE = 1,  /**< FreeType rasterizer + HarfBuzz shaper */
} ltt_font_backend_t;

/* ---- Lifecycle ---------------------------------------------------------- */

/**
 * Load a TrueType/OpenType font from @path at @pixel_size physical pixels.
 * Returns NULL on failure (file not found, bad format, OOM).
 */
ltt_font_t *ltt_font_create(const char *path, int pixel_size);

/**
 * Create a font from an in-memory buffer.
 * @data is copied internally — the caller may free it after this call.
 * Returns NULL on failure.
 */
ltt_font_t *ltt_font_create_from_memory(const uint8_t *data, size_t len,
                                          int pixel_size);

/** Destroy a font context and free all resources. */
void ltt_font_destroy(ltt_font_t *font);

/* ---- Backend ------------------------------------------------------------ */

/**
 * Switch the active font backend.
 * Returns 0 on success, -1 if the requested backend is not available.
 */
int ltt_font_set_backend(ltt_font_t *font, ltt_font_backend_t backend);

/** Return the currently active backend. */
ltt_font_backend_t ltt_font_get_backend(const ltt_font_t *font);

/**
 * Returns 1 if the FreeType/HarfBuzz backend was compiled in and
 * successfully initialised for this font, 0 otherwise.
 */
int ltt_font_has_freetype(const ltt_font_t *font);

/* ---- Metrics ------------------------------------------------------------ */

/** Ascent above the baseline, in pixels (positive). */
int ltt_font_ascent(const ltt_font_t *font);

/** Descent below the baseline, in pixels (positive). */
int ltt_font_descent(const ltt_font_t *font);

/** Recommended line height (ascent + descent + leading), in pixels. */
int ltt_font_line_height(const ltt_font_t *font);

/**
 * Measure the horizontal advance width of UTF-8 text.
 * @len  Byte count, or -1 for strlen.
 */
int ltt_font_measure_text(ltt_font_t *font, const char *utf8, int len);

/* ---- Drawing to pixel buffer -------------------------------------------- */

/**
 * Draw UTF-8 text onto a render target.
 *
 * @target  Pixel buffer + clip rect.
 * @x, @y  Pen position: x = left edge, y = baseline.
 * @utf8   Text (UTF-8).
 * @len    Byte count, or -1 for NUL-terminated.
 * @font   Font context.
 * @color  Text colour (0xAARRGGBB).
 *
 * Glyphs are composited using sRGB-correct linear blending.
 */
void ltt_draw_text(ltt_target_t *target,
                    int x, int y,
                    const char *utf8, int len,
                    ltt_font_t *font,
                    ltt_color_t color);

/** Draw text with extra @letter_spacing pixels between glyphs. */
void ltt_draw_text_spaced(ltt_target_t *target,
                            int x, int y,
                            const char *utf8, int len,
                            ltt_font_t *font,
                            ltt_color_t color,
                            int letter_spacing);

/* ---- Interruptible text rendering --------------------------------------- */

/**
 * State for interruptible (partial) text rendering.
 * Zero-initialise before first use.  Call ltt_text_draw_state_reset()
 * to free internal allocations when done.
 */
typedef struct {
    int      glyph_index;
    int      pen_x, pen_y;
    int      total_glyphs;
    int      complete;
    uint16_t *glyph_ids;
    int16_t  *advances;
} ltt_text_draw_state_t;

/** Free internal allocations in a draw state. */
void ltt_text_draw_state_reset(ltt_text_draw_state_t *state);

/** Deadline callback: returns non-zero when the frame budget has expired. */
typedef int (*ltt_deadline_fn)(void *ctx);

/**
 * Interruptible text rendering.
 *
 * On first call (state zeroed), shapes the text and begins rendering.
 * Checks @deadline every few glyphs; if expired, saves progress and
 * returns 0.  Caller re-invokes next frame with the same @state.
 *
 * @deadline  NULL = no deadline, render all glyphs.
 * Returns 1 when all glyphs rendered, 0 if interrupted.
 */
int ltt_draw_text_partial(ltt_target_t *target,
                            int x, int y,
                            const char *utf8, int len,
                            ltt_font_t *font, ltt_color_t color,
                            ltt_text_draw_state_t *state,
                            ltt_deadline_fn deadline, void *deadline_ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTTYPE_FONT_H */
