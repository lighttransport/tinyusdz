/*
 * palette.c — Grid of color swatches for quick color picking
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/palette.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Number of rows needed for the current colour count. */
static int palette_rows(const lui_palette_t *pal)
{
    if (pal->color_count <= 0 || pal->columns <= 0) return 0;
    return (pal->color_count + pal->columns - 1) / pal->columns;
}

/* Grid content size. */
static void palette_content_size(const lui_palette_t *pal,
                                  int *out_w, int *out_h)
{
    int cols = pal->columns;
    int rows = palette_rows(pal);
    if (cols <= 0 || rows <= 0) {
        *out_w = 0;
        *out_h = 0;
        return;
    }
    *out_w = cols * pal->swatch_size + (cols - 1) * pal->spacing;
    *out_h = rows * pal->swatch_size + (rows - 1) * pal->spacing;
}

/* Hit-test: return colour index at pixel (rx, ry) relative to widget origin,
 * or -1 if no swatch is hit. */
static int palette_hit(const lui_palette_t *pal, int rx, int ry)
{
    int step = pal->swatch_size + pal->spacing;
    if (step <= 0) return -1;

    int col = rx / step;
    int row = ry / step;
    /* Check that the click is within the swatch, not in the spacing. */
    if (rx - col * step >= pal->swatch_size) return -1;
    if (ry - row * step >= pal->swatch_size) return -1;
    if (col < 0 || col >= pal->columns) return -1;

    int idx = row * pal->columns + col;
    if (idx < 0 || idx >= pal->color_count) return -1;
    return idx;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int palette_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_palette_t *pal = (const lui_palette_t *)w;
    (void)user;
    palette_content_size(pal, out_w, out_h);
    return 0;
}

static void palette_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_palette_t *pal = (lui_palette_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, pal->bg_color);

    int step = pal->swatch_size + pal->spacing;
    int corner = pal->swatch_size / 6;
    if (corner < 2) corner = 2;

    for (int i = 0; i < pal->color_count; i++) {
        int col = i % pal->columns;
        int row = i / pal->columns;
        int sx = r.x + col * step;
        int sy = r.y + row * step;
        int ss = pal->swatch_size;

        /* Swatch fill */
        lvg_canvas_fill_rounded_rect(canvas, sx, sy, ss, ss, corner,
                                      pal->colors[i]);

        /* Border / selection indicator */
        if (i == pal->selected) {
            lvg_canvas_stroke_rounded_rect(canvas, sx - 1, sy - 1,
                                            ss + 2, ss + 2, corner,
                                            pal->selected_border, 2);
        } else {
            lvg_canvas_stroke_rounded_rect(canvas, sx, sy, ss, ss, corner,
                                            pal->border_color, 1);
        }
    }
}

static int palette_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_palette_t *pal = (lui_palette_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (lvg_rect_contains_point(&r, mx, my)) {
            int idx = palette_hit(pal, mx - r.x, my - r.y);
            if (idx >= 0) {
                pal->selected = idx;
                if (pal->on_change)
                    pal->on_change(idx, pal->colors[idx],
                                   pal->on_change_user);
                return 1;
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_palette_init(lui_palette_t *pal)
{
    if (!pal) return;

    lui_widget_init(&pal->widget);
    pal->widget.width    = lvg_size_hug(0);
    pal->widget.height   = lvg_size_hug(0);
    pal->widget.measure  = palette_measure;
    pal->widget.draw     = palette_draw;
    pal->widget.on_event = palette_event;

    pal->color_count = 0;
    pal->selected    = -1;
    pal->columns     = 8;
    pal->swatch_size = 24;
    pal->spacing     = 2;

    pal->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    pal->selected_border = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);
    pal->bg_color        = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);

    pal->on_change      = NULL;
    pal->on_change_user = NULL;
}

int lui_palette_add_color(lui_palette_t *pal, lvg_color_t color)
{
    if (!pal || pal->color_count >= LUI_PALETTE_MAX_COLORS)
        return -1;

    int idx = pal->color_count;
    pal->colors[idx] = color;
    pal->color_count++;
    return idx;
}

void lui_palette_set_colors(lui_palette_t *pal,
                             const lvg_color_t *colors, int count)
{
    if (!pal) return;

    pal->color_count = 0;
    pal->selected = -1;
    if (!colors || count <= 0) return;

    int n = count;
    if (n > LUI_PALETTE_MAX_COLORS) n = LUI_PALETTE_MAX_COLORS;
    memcpy(pal->colors, colors, n * sizeof(lvg_color_t));
    pal->color_count = n;
}

void lui_palette_clear(lui_palette_t *pal)
{
    if (!pal) return;
    pal->color_count = 0;
    pal->selected = -1;
}

void lui_palette_set_selected(lui_palette_t *pal, int index)
{
    if (!pal) return;
    if (index < -1 || index >= pal->color_count)
        index = -1;
    pal->selected = index;
}

void lui_palette_add_default_colors(lui_palette_t *pal)
{
    if (!pal) return;

    static const lvg_color_t defaults[16] = {
        /* black       */ LVG_COLOR_RGB(0x00, 0x00, 0x00),
        /* white       */ LVG_COLOR_RGB(0xFF, 0xFF, 0xFF),
        /* red         */ LVG_COLOR_RGB(0xFF, 0x00, 0x00),
        /* green       */ LVG_COLOR_RGB(0x00, 0xCC, 0x00),
        /* blue        */ LVG_COLOR_RGB(0x00, 0x00, 0xFF),
        /* yellow      */ LVG_COLOR_RGB(0xFF, 0xFF, 0x00),
        /* cyan        */ LVG_COLOR_RGB(0x00, 0xFF, 0xFF),
        /* magenta     */ LVG_COLOR_RGB(0xFF, 0x00, 0xFF),
        /* orange      */ LVG_COLOR_RGB(0xFF, 0xA5, 0x00),
        /* purple      */ LVG_COLOR_RGB(0x80, 0x00, 0x80),
        /* pink        */ LVG_COLOR_RGB(0xFF, 0x69, 0xB4),
        /* brown       */ LVG_COLOR_RGB(0x8B, 0x45, 0x13),
        /* dark gray   */ LVG_COLOR_RGB(0x40, 0x40, 0x40),
        /* gray        */ LVG_COLOR_RGB(0x80, 0x80, 0x80),
        /* light gray  */ LVG_COLOR_RGB(0xC0, 0xC0, 0xC0),
        /* teal        */ LVG_COLOR_RGB(0x00, 0x80, 0x80),
    };

    for (int i = 0; i < 16; i++) {
        lui_palette_add_color(pal, defaults[i]);
    }
}
