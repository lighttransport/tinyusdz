/*
 * badge.c — Small status badge / tag label
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/badge.h>
#include <lightvg/canvas.h>

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define BADGE_DOT_SIZE    8
#define BADGE_CHAR_W      5
#define BADGE_CHAR_H     10
#define BADGE_CHAR_ADV    7
#define BADGE_PAD_X       6
#define BADGE_PAD_Y       3
#define BADGE_MIN_W      20

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int badge_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_badge_t *badge = (const lui_badge_t *)w;
    (void)user;

    switch (badge->style) {
    case LUI_BADGE_DOT:
        *out_w = BADGE_DOT_SIZE;
        *out_h = BADGE_DOT_SIZE;
        break;

    case LUI_BADGE_COUNT:
    case LUI_BADGE_LABEL: {
        int text_len = (int)strlen(badge->text);
        int text_w = text_len * BADGE_CHAR_ADV;
        int pill_w = text_w + BADGE_PAD_X * 2;
        if (pill_w < BADGE_MIN_W) pill_w = BADGE_MIN_W;
        *out_w = pill_w;
        *out_h = BADGE_CHAR_H + BADGE_PAD_Y * 2;
        break;
    }

    default:
        *out_w = BADGE_DOT_SIZE;
        *out_h = BADGE_DOT_SIZE;
        break;
    }

    return 0;
}

static void badge_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_badge_t *badge = (lui_badge_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    switch (badge->style) {
    case LUI_BADGE_DOT: {
        int cx = r.x + r.width / 2;
        int cy = r.y + r.height / 2;
        int radius = BADGE_DOT_SIZE / 2;
        lvg_canvas_fill_circle(canvas, cx, cy, radius, badge->bg_color);
        break;
    }

    case LUI_BADGE_COUNT:
    case LUI_BADGE_LABEL: {
        int corner = r.height / 2;
        lvg_canvas_fill_rounded_rect(canvas, r.x, r.y,
                                      r.width, r.height, corner,
                                      badge->bg_color);

        /* Draw text centred in the pill */
        int text_len = (int)strlen(badge->text);
        int text_w = text_len * BADGE_CHAR_ADV;
        int tx = r.x + (r.width - text_w) / 2;
        int ty = r.y + (r.height - BADGE_CHAR_H) / 2;
        for (int c = 0; c < text_len; c++) {
            lvg_canvas_fill_rect(canvas, tx, ty,
                                  BADGE_CHAR_W, BADGE_CHAR_H,
                                  badge->text_color);
            tx += BADGE_CHAR_ADV;
        }
        break;
    }

    default:
        break;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_badge_init(lui_badge_t *badge, lui_badge_style_t style)
{
    if (!badge) return;

    lui_widget_init(&badge->widget);
    badge->widget.width    = lvg_size_hug(0);
    badge->widget.height   = lvg_size_hug(0);
    badge->widget.measure  = badge_measure;
    badge->widget.draw     = badge_draw;
    badge->widget.on_event = NULL;

    badge->style = style;
    badge->text[0] = '\0';

    badge->bg_color   = LVG_COLOR_RGB(0xE0, 0x3E, 0x3E);
    badge->text_color = LVG_COLOR_WHITE;
}

void lui_badge_set_text(lui_badge_t *badge, const char *text)
{
    if (!badge || !text) return;

    int len = (int)strlen(text);
    if (len > 31) len = 31;
    memcpy(badge->text, text, len);
    badge->text[len] = '\0';
}

void lui_badge_set_count(lui_badge_t *badge, int n)
{
    if (!badge) return;

    char buf[32];
    if (n < 0) n = 0;
    if (n > 999) {
        memcpy(buf, "999+", 5);
    } else {
        snprintf(buf, sizeof(buf), "%d", n);
    }
    lui_badge_set_text(badge, buf);
}
