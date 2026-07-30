/*
 * card.c — Content card container widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/card.h>
#include <lightvg/canvas.h>

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define CARD_CHAR_W    5
#define CARD_CHAR_H   10
#define CARD_CHAR_ADV  7
#define CARD_TEXT_PAD   8

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int card_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_card_t *card = (const lui_card_t *)w;
    (void)user;
    int h = 0;
    if (card->show_header) h += card->header_height;
    h += 60;  /* minimum body height */
    if (card->show_footer) h += card->footer_height;
    *out_w = 240;
    *out_h = h;
    return 0;
}

static void card_draw_text(lvg_canvas_t *canvas, int x, int y,
                             const char *text, lvg_color_t color)
{
    if (!text) return;
    int len = (int)strlen(text);
    for (int i = 0; i < len; i++) {
        lvg_canvas_fill_rect(canvas, x, y, CARD_CHAR_W, CARD_CHAR_H, color);
        x += CARD_CHAR_ADV;
    }
}

static void card_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_card_t *card = (lui_card_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int cr = card->corner_radius;

    /* Shadow */
    if (card->shadow_offset > 0) {
        lvg_canvas_fill_rounded_rect(canvas,
            r.x + card->shadow_offset, r.y + card->shadow_offset,
            r.width, r.height, cr, card->shadow_color);
    }

    /* Main card background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height,
                                  cr, card->bg_color);
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height,
                                    cr, card->border_color, 1);

    int y_cursor = r.y;

    /* Header section */
    if (card->show_header) {
        /* Header background — fill top portion including rounded top corners */
        lvg_canvas_fill_rounded_rect(canvas, r.x + 1, r.y + 1,
                                      r.width - 2, card->header_height + cr,
                                      cr, card->header_bg);
        /* Flatten the bottom portion of the header background */
        lvg_canvas_fill_rect(canvas, r.x + 1,
                              r.y + 1 + card->header_height,
                              r.width - 2, cr, card->body_bg);

        /* Title text */
        {
            int tx = r.x + CARD_TEXT_PAD;
            int ty = r.y + (card->header_height - CARD_CHAR_H) / 2;
            if (card->subtitle[0] != '\0') {
                /* Two lines: title near top, subtitle below */
                ty = r.y + (card->header_height / 2) - CARD_CHAR_H - 1;
                card_draw_text(canvas, tx, ty, card->title, card->header_text);
                ty += CARD_CHAR_H + 3;
                card_draw_text(canvas, tx, ty, card->subtitle,
                               card->subtitle_color);
            } else {
                card_draw_text(canvas, tx, ty, card->title, card->header_text);
            }
        }

        /* Separator line */
        lvg_canvas_fill_rect(canvas, r.x + 1,
                              r.y + card->header_height,
                              r.width - 2, 1, card->border_color);

        y_cursor = r.y + card->header_height + 1;
    } else {
        y_cursor = r.y + 1;
    }

    /* Body section */
    {
        int body_h = r.y + r.height - y_cursor;
        if (card->show_footer) body_h -= card->footer_height;
        if (body_h < 0) body_h = 0;

        lvg_canvas_fill_rect(canvas, r.x + 1, y_cursor,
                              r.width - 2, body_h, card->body_bg);

        /* Draw body child widget if set */
        if (card->body_widget) {
            card->body_widget->computed.x = 1;
            card->body_widget->computed.y = y_cursor - r.y;
            card->body_widget->computed.width  = r.width - 2;
            card->body_widget->computed.height = body_h;
            if (card->body_widget->draw)
                card->body_widget->draw(card->body_widget, canvas);
        }

        y_cursor += body_h;
    }

    /* Footer section */
    if (card->show_footer) {
        /* Separator line */
        lvg_canvas_fill_rect(canvas, r.x + 1, y_cursor,
                              r.width - 2, 1, card->border_color);
        y_cursor += 1;

        /* Footer background — handle rounded bottom corners */
        int fh = card->footer_height - 1;
        lvg_canvas_fill_rounded_rect(canvas, r.x + 1, y_cursor - cr,
                                      r.width - 2, fh + cr, cr,
                                      card->footer_bg);
        /* Flatten the top portion */
        lvg_canvas_fill_rect(canvas, r.x + 1, y_cursor,
                              r.width - 2, fh > cr ? fh - cr : 0,
                              card->footer_bg);

        /* Draw footer child widget if set */
        if (card->footer_widget) {
            card->footer_widget->computed.x = 1;
            card->footer_widget->computed.y = y_cursor - r.y;
            card->footer_widget->computed.width  = r.width - 2;
            card->footer_widget->computed.height = fh;
            if (card->footer_widget->draw)
                card->footer_widget->draw(card->footer_widget, canvas);
        }
    }
}

static int card_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_card_t *card = (lui_card_t *)w;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT && card->on_click) {
            lvg_rect_t r = lui_widget_absolute_rect(w);
            if (lvg_rect_contains_point(&r,
                    event->data.mouse_button.x,
                    event->data.mouse_button.y)) {
                card->on_click(card->on_click_user);
                return 1;
            }
        }
        break;

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_card_init(lui_card_t *card)
{
    if (!card) return;

    lui_widget_init(&card->widget);
    card->widget.width    = lvg_size_hug(240);
    card->widget.height   = lvg_size_hug(0);
    card->widget.measure  = card_measure;
    card->widget.draw     = card_draw;
    card->widget.on_event = card_event;
    card->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    card->title[0]    = '\0';
    card->subtitle[0] = '\0';
    card->show_header = true;
    card->show_footer = false;

    card->corner_radius  = 8;
    card->header_height  = 40;
    card->footer_height  = 36;

    card->shadow_offset  = 2;
    card->shadow_color   = LVG_COLOR_ARGB(0x40, 0x00, 0x00, 0x00);

    card->bg_color       = LVG_COLOR_RGB(0x24, 0x28, 0x30);
    card->header_bg      = LVG_COLOR_RGB(0x1E, 0x22, 0x2A);
    card->header_text    = LVG_COLOR_RGB(0xE0, 0xE4, 0xEC);
    card->body_bg        = LVG_COLOR_RGB(0x24, 0x28, 0x30);
    card->footer_bg      = LVG_COLOR_RGB(0x1E, 0x22, 0x2A);
    card->border_color   = LVG_COLOR_RGB(0x38, 0x3C, 0x44);
    card->subtitle_color = LVG_COLOR_RGB(0x80, 0x88, 0x98);

    card->body_widget   = NULL;
    card->footer_widget = NULL;

    card->on_click      = NULL;
    card->on_click_user = NULL;
}

void lui_card_set_title(lui_card_t *card, const char *title,
                         const char *subtitle)
{
    if (!card) return;

    if (title) {
        int len = (int)strlen(title);
        if (len > 63) len = 63;
        memcpy(card->title, title, len);
        card->title[len] = '\0';
    } else {
        card->title[0] = '\0';
    }

    if (subtitle) {
        int len = (int)strlen(subtitle);
        if (len > 31) len = 31;
        memcpy(card->subtitle, subtitle, len);
        card->subtitle[len] = '\0';
    } else {
        card->subtitle[0] = '\0';
    }
}

void lui_card_set_body(lui_card_t *card, lui_widget_t *widget)
{
    if (!card) return;
    card->body_widget = widget;
}

void lui_card_set_footer(lui_card_t *card, lui_widget_t *widget)
{
    if (!card) return;
    card->footer_widget = widget;
    card->show_footer = (widget != NULL);
}
