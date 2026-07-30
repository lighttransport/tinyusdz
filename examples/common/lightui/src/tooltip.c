/*
 * tooltip.c — Tooltip popup widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/tooltip.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int tt_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_tooltip_t *tt = (const lui_tooltip_t *)w;
    (void)user;
    *out_w = tt->text_len * 7 + tt->padding * 2;
    *out_h = 10 + tt->padding * 2;
    return 0;
}

static void tt_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_tooltip_t *tt = (lui_tooltip_t *)w;
    if (!tt->visible || tt->text_len <= 0) return;

    int tw = tt->text_len * 7 + tt->padding * 2;
    int th = 10 + tt->padding * 2;
    int tx = tt->target_x;
    int ty = tt->target_y + tt->offset_y;

    /* Background */
    lvg_canvas_fill_rounded_rect(canvas, tx, ty, tw, th,
                                  tt->corner_radius, tt->bg);
    lvg_canvas_stroke_rounded_rect(canvas, tx, ty, tw, th,
                                    tt->corner_radius, tt->border_color, 1);

    /* Text (char rectangles) */
    int cx = tx + tt->padding;
    int cy = ty + tt->padding;
    for (int i = 0; i < tt->text_len; i++) {
        lvg_canvas_fill_rect(canvas, cx, cy, 5, 10, tt->text_color);
        cx += 7;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_tooltip_init(lui_tooltip_t *tt)
{
    if (!tt) return;

    lui_widget_init(&tt->widget);
    tt->widget.width   = lvg_size_hug(0);
    tt->widget.height  = lvg_size_hug(0);
    tt->widget.measure = tt_measure;
    tt->widget.draw    = tt_draw;
    tt->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    tt->text[0]      = '\0';
    tt->text_len     = 0;
    tt->visible      = false;
    tt->hover_time   = 0.0f;
    tt->delay        = 0.5f;
    tt->target_x     = 0;
    tt->target_y     = 0;
    tt->padding      = 6;
    tt->corner_radius = 4;
    tt->offset_y     = 16;

    tt->bg           = LVG_COLOR_RGB(0x28, 0x2C, 0x34);
    tt->text_color   = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    tt->border_color = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
}

void lui_tooltip_set_text(lui_tooltip_t *tt, const char *text)
{
    if (!tt || !text) return;
    int len = (int)strlen(text);
    if (len > LUI_TOOLTIP_MAX_TEXT) len = LUI_TOOLTIP_MAX_TEXT;
    memcpy(tt->text, text, len);
    tt->text[len] = '\0';
    tt->text_len = len;
}

void lui_tooltip_show(lui_tooltip_t *tt, int x, int y)
{
    if (!tt) return;
    tt->visible = true;
    tt->target_x = x;
    tt->target_y = y;
}

void lui_tooltip_hide(lui_tooltip_t *tt)
{
    if (!tt) return;
    tt->visible = false;
    tt->hover_time = 0.0f;
}

bool lui_tooltip_update(lui_tooltip_t *tt, float dt, bool hovering,
                         int mx, int my)
{
    if (!tt) return false;

    if (hovering) {
        tt->hover_time += dt;
        if (!tt->visible && tt->hover_time >= tt->delay) {
            lui_tooltip_show(tt, mx, my);
            return true;
        }
    } else {
        if (tt->visible) {
            lui_tooltip_hide(tt);
            return true;
        }
        tt->hover_time = 0.0f;
    }
    return false;
}
