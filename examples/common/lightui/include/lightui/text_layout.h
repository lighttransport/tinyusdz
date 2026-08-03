/*
 * lightui/text_layout.h — Span-based rich text layout engine
 *
 * Designed for three primary use cases:
 *   • Terminal output  — monospace text, per-cell fg/bg colour, cursor blink
 *   • Markdown display — bold/italic/code/underline spans, paragraphs
 *   • Inline images    — pixel surfaces embedded in a text flow
 *
 * Architecture:
 *
 *   1. Application appends spans (text or image).
 *   2. lui_text_layout_build() performs greedy word-wrap and computes line/run
 *      geometry.  This step is cheap enough to call on every text change.
 *   3. lui_text_layout_draw() renders, skipping lines outside the dirty region
 *      and honouring a frame-clock deadline.
 *
 * Ownership: text spans store a pointer into caller-owned UTF-8 data.
 * The caller must keep the string data alive until the layout is cleared.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TEXT_LAYOUT_H
#define LIGHTUI_TEXT_LAYOUT_H

#include <lightvg/canvas.h>
#include <lightvg/dirty.h>
#include "frame_clock.h"
#include "font.h"
#include <lightvg/types.h>

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Text style flags ---------------------------------------------------- */

#define LUI_TEXT_BOLD          0x01u
#define LUI_TEXT_ITALIC        0x02u
#define LUI_TEXT_UNDERLINE     0x04u
#define LUI_TEXT_STRIKETHROUGH 0x08u
#define LUI_TEXT_CODE          0x10u   /* monospace code rendering hint */

/* ---- Span ---------------------------------------------------------------- */

typedef enum {
    LUI_SPAN_TEXT  = 0,
    LUI_SPAN_IMAGE = 1,
    LUI_SPAN_BREAK = 2    /* forced line break; other fields ignored */
} lui_span_type_t;

typedef struct {
    lui_span_type_t type;
    union {
        struct {
            const char  *utf8;   /* caller-owned; must outlive layout       */
            int          len;    /* byte count; -1 = strlen                 */
            lvg_color_t  fg;     /* foreground colour                       */
            lvg_color_t  bg;     /* background colour; LVG_COLOR_TRANSPARENT = none */
            uint8_t      flags;  /* LUI_TEXT_* bitmask                      */
            lui_font_t  *font;   /* NULL = use layout's default_font        */
        } text;
        struct {
            const lvg_surface_t *surface;  /* caller-owned pixel data       */
            int                  w, h;     /* display size; 0 = natural     */
        } image;
    };
} lui_span_t;

/* ---- Built geometry (output of build()) ---------------------------------- */

/*
 * A positioned run: a contiguous piece of text or one image on a single line.
 * x, y are offsets from the layout origin passed to lui_text_layout_draw().
 * For text: y is the baseline.
 * For images: y is the top edge of the image.
 */
typedef struct {
    int          x, y;
    int          width, height;
    bool         is_image;
    /* text fields */
    const char  *utf8;
    int          len;
    lvg_color_t  fg, bg;
    uint8_t      flags;
    lui_font_t  *font;
    /* image fields */
    const lvg_surface_t *image;
} lui_run_t;

/* A laid-out line: slice into the run array. */
typedef struct {
    int y;          /* top of line (layout-relative)  */
    int height;     /* total line height in pixels     */
    int baseline;   /* pixels from y to the text baseline */
    int run_start;  /* index into layout->runs         */
    int run_count;
} lui_line_t;

/* ---- Layout --------------------------------------------------------------- */

typedef struct {
    /* Configuration */
    lui_font_t  *default_font;
    int          max_width;    /* wrap width in pixels; 0 = no wrapping */
    lvg_color_t  default_fg;
    int          letter_spacing; /* extra pixels between characters (default 0) */
    int          line_spacing;   /* extra pixels between lines (default 0)      */

    /* Input spans (dynamic array) */
    lui_span_t  *spans;
    int          span_count, span_cap;

    /* Built output (dynamic arrays; valid after build()) */
    lui_run_t   *runs;
    int          run_count, run_cap;
    lui_line_t  *lines;
    int          line_count, line_cap;
    int          total_width;   /* widest line in pixels */
    int          total_height;  /* sum of all line heights */

    int          needs_rebuild; /* 1 = build() must be called before draw() */
} lui_text_layout_t;

/* ---- Lifecycle ----------------------------------------------------------- */

/*
 * Initialise a layout.  @font is the fallback font for spans that omit one.
 * @max_width is the wrap width in pixels (0 = no line-wrap).
 */
void lui_text_layout_init(lui_text_layout_t *tl,
                           lui_font_t *font, int max_width);

/* Free all heap storage.  Does not free fonts or surfaces owned by spans. */
void lui_text_layout_destroy(lui_text_layout_t *tl);

/* Remove all spans and invalidate the build output. */
void lui_text_layout_clear(lui_text_layout_t *tl);

/* ---- Span API ------------------------------------------------------------ */

/*
 * Append a text span.
 * @utf8   Caller-owned UTF-8 string (may be NUL-terminated or length-bounded).
 * @len    Byte count, or -1 for strlen.
 * @font   NULL to use layout default.
 */
void lui_text_layout_add_text(lui_text_layout_t *tl,
                               const char *utf8, int len,
                               lvg_color_t fg, lvg_color_t bg,
                               uint8_t flags, lui_font_t *font);

/* Append an inline image span.  @w, @h are display pixels (0 = natural). */
void lui_text_layout_add_image(lui_text_layout_t *tl,
                                const lvg_surface_t *surface, int w, int h);

/* Append a forced line break. */
void lui_text_layout_add_break(lui_text_layout_t *tl);

/* ---- Build --------------------------------------------------------------- */

/*
 * Compute line/run geometry from the current span list.
 * Must be called after any span modification and before draw().
 * Returns the total layout height in pixels.
 */
int lui_text_layout_build(lui_text_layout_t *tl);

/* ---- Draw ---------------------------------------------------------------- */

/*
 * Render the layout onto @canvas at (@x, @y) (layout origin = top-left).
 *
 * @dirty  If non-NULL, lines outside the dirty region are skipped.
 *         Pass NULL to draw everything.
 * @clk    If non-NULL, drawing stops when the deadline expires.
 *         Partially-drawn output is valid; the caller can present it.
 *         Pass NULL to draw without a time limit.
 *
 * build() must have been called since the last span modification.
 */
void lui_text_layout_draw(const lui_text_layout_t *tl,
                           lvg_canvas_t *canvas,
                           int x, int y,
                           const lvg_dirty_t *dirty,
                           lui_frame_clock_t *clk);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TEXT_LAYOUT_H */
