/*
 * src/text_layout.c — Span-based rich text layout engine
 *
 * Build algorithm (greedy line-wrap):
 *
 *   - pen_y tracks the top of the current line.
 *   - For each TEXT span: tokenise into words on ASCII whitespace / newlines.
 *     Measure each word with lui_font_measure_text().  Wrap before a word if
 *     adding it would exceed max_width (skip wrap at column 0 so overlong
 *     single words always get placed).  Spaces after a word are measured and
 *     added to pen_x but produce no run (trailing spaces are also dropped).
 *   - For IMAGE spans: treat as a single box.
 *   - BREAK spans: flush the current line unconditionally.
 *   - On flush_line(): finalise y-coordinates for all runs on the line using
 *     the line's baseline, record the lui_line_t, and advance pen_y.
 *
 * Draw algorithm (dirty-region culling + deadline):
 *
 *   For each line whose bounding rect intersects the dirty set:
 *     For each run on the line:
 *       Draw text (background fill + glyph composite) or blit image.
 *     After each line: check frame_clock_expired(); bail if true.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/text_layout.h>
#include "utf8_util.h"

#include <stdlib.h>
#include <string.h>

/* ---- Dynamic array helpers ----------------------------------------------- */

#define DA_PUSH(arr, count, cap, item)                                      \
    do {                                                                    \
        if ((count) >= (cap)) {                                             \
            int new_cap = (cap) ? (cap) * 2 : 16;                          \
            void *p = realloc((arr), (size_t)new_cap * sizeof(*(arr)));     \
            if (!p) break;                                                  \
            (arr) = p; (cap) = new_cap;                                     \
        }                                                                   \
        (arr)[(count)++] = (item);                                          \
    } while (0)

/* ---- Lifecycle ----------------------------------------------------------- */

void lui_text_layout_init(lui_text_layout_t *tl,
                           lui_font_t *font, int max_width)
{
    memset(tl, 0, sizeof(*tl));
    tl->default_font   = font;
    tl->max_width      = max_width;
    tl->default_fg     = LVG_COLOR_BLACK;
    tl->needs_rebuild  = 1;
}

void lui_text_layout_destroy(lui_text_layout_t *tl)
{
    free(tl->spans);
    free(tl->runs);
    free(tl->lines);
    memset(tl, 0, sizeof(*tl));
}

void lui_text_layout_clear(lui_text_layout_t *tl)
{
    tl->span_count = 0;
    tl->run_count  = 0;
    tl->line_count = 0;
    tl->needs_rebuild = 1;
}

/* ---- Span API ------------------------------------------------------------ */

void lui_text_layout_add_text(lui_text_layout_t *tl,
                               const char *utf8, int len,
                               lvg_color_t fg, lvg_color_t bg,
                               uint8_t flags, lui_font_t *font)
{
    if (!utf8) return;
    lui_span_t s;
    memset(&s, 0, sizeof(s));
    s.type       = LUI_SPAN_TEXT;
    s.text.utf8  = utf8;
    s.text.len   = (len < 0) ? (int)strlen(utf8) : len;
    s.text.fg    = fg;
    s.text.bg    = bg;
    s.text.flags = flags;
    s.text.font  = font;
    DA_PUSH(tl->spans, tl->span_count, tl->span_cap, s);
    tl->needs_rebuild = 1;
}

void lui_text_layout_add_image(lui_text_layout_t *tl,
                                const lvg_surface_t *surface, int w, int h)
{
    if (!surface) return;
    lui_span_t s;
    memset(&s, 0, sizeof(s));
    s.type         = LUI_SPAN_IMAGE;
    s.image.surface = surface;
    s.image.w      = w > 0 ? w : surface->width;
    s.image.h      = h > 0 ? h : surface->height;
    DA_PUSH(tl->spans, tl->span_count, tl->span_cap, s);
    tl->needs_rebuild = 1;
}

void lui_text_layout_add_break(lui_text_layout_t *tl)
{
    lui_span_t s;
    memset(&s, 0, sizeof(s));
    s.type = LUI_SPAN_BREAK;
    DA_PUSH(tl->spans, tl->span_count, tl->span_cap, s);
    tl->needs_rebuild = 1;
}

/* ---- Build --------------------------------------------------------------- */

/* Build state lives on the stack of lui_text_layout_build(). */
typedef struct {
    int pen_x, pen_y;
    int line_h;        /* max height seen so far on current line  */
    int line_baseline; /* max ascent seen so far on current line  */
    int line_run_start;

    /* Cached default-font metrics — computed once to avoid repeating the
     * same 4 function calls for every span when font never changes. */
    int  def_lh, def_asc;   /* default font line_height / ascent */
    int  def_space_w;       /* lui_font_measure_text(" ") */
    int  def_tab_w;         /* lui_font_measure_text("\t") */
} bstate_t;

/* Push a finalised line using accumulated state. */
static void flush_line(lui_text_layout_t *tl, bstate_t *st)
{
    int run_start = st->line_run_start;
    int run_end   = tl->run_count;
    int baseline  = st->line_baseline;
    int lh        = st->line_h;

    /* Finalise y-coordinates for all runs on this line. */
    for (int i = run_start; i < run_end; i++) {
        lui_run_t *r = &tl->runs[i];
        if (!r->is_image) {
            /* Text: y = baseline */
            r->y = st->pen_y + baseline;
        } else {
            /* Image: sit on the baseline (bottom of image = baseline). */
            r->y = st->pen_y + baseline - r->height;
            if (r->y < st->pen_y) r->y = st->pen_y;
        }
    }

    if (run_end > run_start || lh > 0) {
        int total_lh = lh + tl->line_spacing;
        lui_line_t line;
        line.y         = st->pen_y;
        line.height    = lh;
        line.baseline  = baseline;
        line.run_start = run_start;
        line.run_count = run_end - run_start;
        DA_PUSH(tl->lines, tl->line_count, tl->line_cap, line);
        st->pen_y += total_lh;
    }

    st->pen_x          = 0;
    st->line_h         = 0;
    st->line_baseline  = 0;
    st->line_run_start = tl->run_count;
}

/* Add a text run at (pen_x, 0) -- y is finalised in flush_line(). */
static void add_text_run(lui_text_layout_t *tl, bstate_t *st,
                          const char *utf8, int len,
                          lvg_color_t fg, lvg_color_t bg,
                          uint8_t flags, lui_font_t *font,
                          int word_w, int lh, int asc)
{
    lui_run_t r = {
        .x = st->pen_x,
        .y = 0,
        .width  = word_w,
        .height = lh,
        .is_image = false,
        .utf8 = utf8,
        .len  = len,
        .fg = fg,
        .bg = bg,
        .flags = flags,
        .font = font,
    };
    DA_PUSH(tl->runs, tl->run_count, tl->run_cap, r);

    st->pen_x += word_w;
    if (lh  > st->line_h)        st->line_h        = lh;
    if (asc > st->line_baseline) st->line_baseline  = asc;

    if (st->pen_x > tl->total_width) tl->total_width = st->pen_x;
}

static void layout_text_span(lui_text_layout_t *tl, bstate_t *st,
                               const lui_span_t *span)
{
    lui_font_t *font = span->text.font ? span->text.font : tl->default_font;
    if (!font) return;

    const char *p   = span->text.utf8;
    const char *end = p + span->text.len;

    /* Use cached default-font metrics when span uses the layout default font
     * (the common case — avoids 4 function calls per span). */
    int lh, asc, space_w, tab_w;
    if (span->text.font) {
        lh      = lui_font_line_height(font);
        asc     = lui_font_ascent(font);
        space_w = lui_font_measure_text(font, " ", 1);
        tab_w   = lui_font_measure_text(font, "\t", 1);
    } else {
        lh      = st->def_lh;
        asc     = st->def_asc;
        space_w = st->def_space_w;
        tab_w   = st->def_tab_w;
    }

    /* Ensure at least some line height even for the first (empty) line. */
    if (lh  > st->line_h)        st->line_h        = lh;
    if (asc > st->line_baseline) st->line_baseline  = asc;

    while (p < end) {
        /* Consume a newline. */
        if (*p == '\n') {
            flush_line(tl, st);
            /* Restore line metrics for next line using this font. */
            st->line_h        = lh;
            st->line_baseline = asc;
            p++;
            continue;
        }

        /* Skip spaces (track their width for pen advance, but produce no run). */
        if (*p == ' ' || *p == '\t') {
            const char *sp = p;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (st->pen_x > 0) {  /* Don't indent at start of line */
                int nsp = (int)(p - sp);
                int sw = 0;
                for (int si = 0; si < nsp; si++)
                    sw += (sp[si] == '\t') ? tab_w : space_w;
                sw += nsp * tl->letter_spacing;
                st->pen_x += sw;
            }
            continue;
        }

        /* Find end of word (next whitespace or control char). */
        const char *word = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n') p++;
        int wlen  = (int)(p - word);
        int word_w = lui_font_measure_text(font, word, wlen);
        /* Count characters for letter spacing (UTF-8 aware: count lead bytes) */
        if (tl->letter_spacing != 0) {
            int nchars = 0;
            for (int ci = 0; ci < wlen; ci++)
                if ((word[ci] & 0xC0) != 0x80) nchars++;
            if (nchars > 0) word_w += (nchars - 1) * tl->letter_spacing;
        }

        /* Wrap if word would overflow the line (but never at column 0). */
        if (tl->max_width > 0 && st->pen_x > 0 &&
            st->pen_x + word_w > tl->max_width) {
            flush_line(tl, st);
            st->line_h        = lh;
            st->line_baseline = asc;
        }

        add_text_run(tl, st, word, wlen,
                     span->text.fg, span->text.bg,
                     span->text.flags, font,
                     word_w, lh, asc);
    }
}

static void layout_image_span(lui_text_layout_t *tl, bstate_t *st,
                                const lui_span_t *span)
{
    int iw = span->image.w;
    int ih = span->image.h;

    /* Wrap before image if it doesn't fit on the current line. */
    if (tl->max_width > 0 && st->pen_x > 0 &&
        st->pen_x + iw > tl->max_width) {
        flush_line(tl, st);
    }

    lui_run_t r = {
        .x = st->pen_x,
        .y = 0,
        .width  = iw,
        .height = ih,
        .is_image = true,
        .image = span->image.surface,
    };
    DA_PUSH(tl->runs, tl->run_count, tl->run_cap, r);

    st->pen_x += iw;
    if (ih  > st->line_h)        st->line_h        = ih;
    if (ih  > st->line_baseline) st->line_baseline  = ih;

    if (st->pen_x > tl->total_width) tl->total_width = st->pen_x;
}

int lui_text_layout_build(lui_text_layout_t *tl)
{
    tl->run_count   = 0;
    tl->line_count  = 0;
    tl->total_width = 0;
    tl->total_height= 0;

    /* Pre-allocate runs using a rough heuristic to reduce DA_PUSH realloc
     * copies.  Each text span may contain many words after row batching,
     * so estimate generously: average of 8 chars/word × 4 words/line. */
    int est_runs  = tl->span_count * 20;
    if (est_runs  > tl->run_cap) {
        lui_run_t *p = (lui_run_t *)realloc(tl->runs, (size_t)est_runs * sizeof(lui_run_t));
        if (p) { tl->runs = p; tl->run_cap = est_runs; }
    }
    int est_lines = tl->span_count;
    if (est_lines > tl->line_cap) {
        lui_line_t *p = (lui_line_t *)realloc(tl->lines, (size_t)est_lines * sizeof(lui_line_t));
        if (p) { tl->lines = p; tl->line_cap = est_lines; }
    }

    bstate_t st;
    memset(&st, 0, sizeof(st));

    /* Seed line height from default font so empty layouts have correct height,
     * and cache the default-font metrics so layout_text_span can avoid 4
     * redundant function calls per span. */
    if (tl->default_font) {
        st.line_h        = lui_font_line_height(tl->default_font);
        st.line_baseline = lui_font_ascent(tl->default_font);
        st.def_lh        = st.line_h;
        st.def_asc       = st.line_baseline;
        st.def_space_w   = lui_font_measure_text(tl->default_font, " ", 1);
        st.def_tab_w     = lui_font_measure_text(tl->default_font, "\t", 1);
    }

    for (int i = 0; i < tl->span_count; i++) {
        const lui_span_t *sp = &tl->spans[i];
        switch (sp->type) {
        case LUI_SPAN_TEXT:  layout_text_span (tl, &st, sp); break;
        case LUI_SPAN_IMAGE: layout_image_span(tl, &st, sp); break;
        case LUI_SPAN_BREAK:
            flush_line(tl, &st);
            if (tl->default_font) {
                st.line_h        = st.def_lh;
                st.line_baseline = st.def_asc;
            }
            break;
        }
    }

    /* Flush the last line (may be non-empty). */
    flush_line(tl, &st);

    tl->total_height  = st.pen_y;
    tl->needs_rebuild = 0;
    return tl->total_height;
}

/* ---- Draw ---------------------------------------------------------------- */

/*
 * Draw one run.  The canvas clip is already set to the dirty intersection;
 * we just need to paint the run.
 */
static void draw_run(const lui_run_t *r, lvg_canvas_t *canvas,
                     int layout_x, int layout_y,
                     int line_baseline, int line_top, int line_h,
                     int letter_spacing)
{
    (void)line_baseline;
    int rx = layout_x + r->x;

    if (r->is_image) {
        int ry = layout_y + r->y;
        if (r->width == r->image->width && r->height == r->image->height) {
            lvg_canvas_blit(canvas, rx, ry, r->image, NULL);
        } else {
            lvg_canvas_draw_image(canvas, rx, ry, r->width, r->height,
                                  r->image, NULL, LVG_IMAGE_FILTER_BILINEAR);
        }
    } else {
        /* Draw background fill behind this word if requested. */
        if (LVG_COLOR_A(r->bg) > 0) {
            int ry_top = layout_y + line_top;
            lvg_canvas_fill_rect(canvas, rx, ry_top, r->width, line_h, r->bg);
        }

        /* Draw text; r->y is the baseline. */
        int ry = layout_y + r->y;
        if (letter_spacing == 0) {
            lui_canvas_draw_text(canvas, rx, ry, r->utf8, r->len,
                                 r->font, r->fg);
        } else {
            /* Render character by character with extra spacing. */
            int cx = rx;
            int pos = 0;
            while (pos < r->len) {
                int clen = lui__utf8_cp_len(r->utf8, pos, r->len);
                if (clen <= 0) break;
                lui_canvas_draw_text(canvas, cx, ry, r->utf8 + pos, clen,
                                     r->font, r->fg);
                int cw = lui_font_measure_text(r->font, r->utf8 + pos, clen);
                cx += cw + letter_spacing;
                pos += clen;
            }
        }

        /* Underline */
        if (r->flags & LUI_TEXT_UNDERLINE) {
            int uy = ry + 2;
            lvg_canvas_draw_line(canvas, rx, uy, rx + r->width, uy, r->fg, 1);
        }
        /* Strikethrough */
        if (r->flags & LUI_TEXT_STRIKETHROUGH) {
            int sy = layout_y + line_top + (line_h / 2);
            lvg_canvas_draw_line(canvas, rx, sy, rx + r->width, sy, r->fg, 1);
        }
    }
}

void lui_text_layout_draw(const lui_text_layout_t *tl,
                           lvg_canvas_t *canvas,
                           int x, int y,
                           const lvg_dirty_t *dirty,
                           lui_frame_clock_t *clk)
{
    if (!tl || !canvas || tl->needs_rebuild) return;

    for (int li = 0; li < tl->line_count; li++) {
        const lui_line_t *line = &tl->lines[li];

        /* Absolute line bounding rect in canvas coords. */
        lvg_rect_t line_rect = lvg_rect_make(x, y + line->y,
                                              tl->total_width > 0
                                                  ? tl->total_width
                                                  : canvas->_surface->width,
                                              line->height);

        /* Skip line entirely if it doesn't intersect the dirty region. */
        if (dirty && !lvg_dirty_test(dirty, &line_rect)) continue;

        /* Draw each run, clipped to dirty intersections. */
        for (int ri = line->run_start;
             ri < line->run_start + line->run_count; ri++) {
            const lui_run_t *run = &tl->runs[ri];

            lvg_rect_t run_rect = lvg_rect_make(x + run->x, y + line->y,
                                                  run->width, line->height);

            if (dirty && !lvg_dirty_test(dirty, &run_rect)) continue;

            if (dirty && !dirty->all) {
                /* Draw clipped to each dirty rect that intersects this run. */
                lvg_rect_t saved = canvas->_clip;
                for (int di = 0; di < dirty->count; di++) {
                    lvg_rect_t clip = lvg_rect_intersect(&run_rect,
                                                           &dirty->rects[di]);
                    if (lvg_rect_is_empty(&clip)) continue;
                    clip = lvg_rect_intersect(&clip, &saved);
                    if (lvg_rect_is_empty(&clip)) continue;
                    lvg_canvas_set_clip(canvas, &clip);
                    draw_run(run, canvas, x, y,
                             line->baseline, line->y, line->height,
                             tl->letter_spacing);
                }
                canvas->_clip = saved;
            } else {
                draw_run(run, canvas, x, y,
                         line->baseline, line->y, line->height,
                         tl->letter_spacing);
            }
        }

        /* Early termination after each line. */
        if (clk && lui_frame_clock_expired(clk)) break;
    }
}
