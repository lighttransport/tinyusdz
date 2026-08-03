/*
 * radio.c — Radio button group widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/radio.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define RADIO_CIRCLE_DIAMETER  14
#define RADIO_INNER_RADIUS      4
#define RADIO_LABEL_GAP         6   /* gap between circle and text */
#define RADIO_CHAR_W            5
#define RADIO_CHAR_H           10
#define RADIO_CHAR_ADV          7

static int radio_text_width(const lui_radio_t *r, const char *text)
{
#ifdef LUI_HAVE_FONTS
    if (r->font)
        return lui_font_measure_text(r->font, text, -1);
#else
    (void)r;
#endif
    return (int)strlen(text) * RADIO_CHAR_ADV;
}

static int radio_text_height(const lui_radio_t *r)
{
#ifdef LUI_HAVE_FONTS
    if (r->font)
        return lui_font_line_height(r->font);
#else
    (void)r;
#endif
    return RADIO_CHAR_H;
}

static int radio_circle_diameter(const lui_radio_t *r)
{
    return r->circle_diameter > 0 ? r->circle_diameter : RADIO_CIRCLE_DIAMETER;
}

static int radio_inner_radius(const lui_radio_t *r)
{
    return r->inner_radius > 0 ? r->inner_radius : RADIO_INNER_RADIUS;
}

static int radio_label_gap(const lui_radio_t *r)
{
    return r->label_gap > 0 ? r->label_gap : RADIO_LABEL_GAP;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Compute the bounding rect for item at @idx relative to widget origin. */
static void radio_item_rect(const lui_radio_t *r, int idx,
                              int wx, int wy,
                              int *ix, int *iy, int *iw, int *ih)
{
    int text_w = radio_text_width(r, r->items[idx]);
    int circle_d = radio_circle_diameter(r);
    int label_gap = radio_label_gap(r);
    int item_w = circle_d + label_gap + text_w;
    int text_h = radio_text_height(r);
    int item_h = circle_d > text_h ? circle_d : text_h;

    if (r->orientation == LUI_RADIO_COLUMN) {
        *ix = wx;
        *iy = wy + idx * (item_h + r->item_spacing);
    } else {
        int offset = 0;
        for (int i = 0; i < idx; i++) {
            int tw = radio_text_width(r, r->items[i]);
            offset += circle_d + label_gap + tw + r->item_spacing;
        }
        *ix = wx + offset;
        *iy = wy;
    }
    *iw = item_w;
    *ih = item_h;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int radio_measure(const lui_widget_t *w, int *out_w, int *out_h,
                           void *user)
{
    const lui_radio_t *r = (const lui_radio_t *)w;
    (void)user;

    int text_h = radio_text_height(r);
    int circle_d = radio_circle_diameter(r);
    int label_gap = radio_label_gap(r);
    int item_h = circle_d > text_h ? circle_d : text_h;

    if (r->item_count <= 0) {
        *out_w = circle_d;
        *out_h = circle_d;
        return 0;
    }

    if (r->orientation == LUI_RADIO_COLUMN) {
        /* Width = widest item */
        int max_w = 0;
        for (int i = 0; i < r->item_count; i++) {
            int text_w = radio_text_width(r, r->items[i]);
            int w2 = circle_d + label_gap + text_w;
            if (w2 > max_w) max_w = w2;
        }
        *out_w = max_w;
        *out_h = r->item_count * item_h +
                 (r->item_count - 1) * r->item_spacing;
    } else {
        /* Row: width = sum of items + spacing */
        int total_w = 0;
        for (int i = 0; i < r->item_count; i++) {
            int text_w = radio_text_width(r, r->items[i]);
            total_w += circle_d + label_gap + text_w;
            if (i > 0) total_w += r->item_spacing;
        }
        *out_w = total_w;
        *out_h = item_h;
    }
    return 0;
}

static void radio_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_radio_t *r = (lui_radio_t *)w;
    lvg_rect_t rect = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&rect)) return;

    for (int i = 0; i < r->item_count; i++) {
        int ix, iy, iw, ih;
        radio_item_rect(r, i, rect.x, rect.y, &ix, &iy, &iw, &ih);
        int circle_d = radio_circle_diameter(r);
        int label_gap = radio_label_gap(r);

        /* Outer circle */
        int cx = ix + circle_d / 2;
        int cy = iy + ih / 2;
        int outer_r = circle_d / 2;

        lvg_canvas_fill_circle(canvas, cx, cy, outer_r, r->circle_color);
        lvg_canvas_stroke_circle(canvas, cx, cy, outer_r, r->border_color,
                                  r->border_width > 0 ? r->border_width : 1);

        /* Inner filled circle when selected */
        if (i == r->selected) {
            lvg_canvas_fill_circle(canvas, cx, cy,
                                    radio_inner_radius(r), r->selected_color);
        }

        /* Text label */
        const char *label = r->items[i];
        int tx = ix + circle_d + label_gap;
#ifdef LUI_HAVE_FONTS
        if (r->font) {
            int ty = iy + lui_font_ascent(r->font) +
                     (ih - lui_font_line_height(r->font)) / 2;
            lui_canvas_draw_text(canvas, tx, ty, label, -1, r->font,
                                 r->text_color);
        } else {
#endif
            int label_len = (int)strlen(label);
            int ty = iy + (ih - RADIO_CHAR_H) / 2;
            for (int c = 0; c < label_len; c++) {
                lvg_canvas_fill_rect(canvas, tx, ty,
                                      RADIO_CHAR_W, RADIO_CHAR_H, r->text_color);
                tx += RADIO_CHAR_ADV;
            }
#ifdef LUI_HAVE_FONTS
        }
#endif
    }
}

static int radio_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_radio_t *r = (lui_radio_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        lvg_rect_t rect = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        /* Check if click is within widget bounds first */
        if (!lvg_rect_contains_point(&rect, mx, my))
            return 0;

        /* Find which item was clicked */
        for (int i = 0; i < r->item_count; i++) {
            int ix, iy, iw, ih;
            radio_item_rect(r, i, rect.x, rect.y, &ix, &iy, &iw, &ih);

            lvg_rect_t item_r = lvg_rect_make(ix, iy, iw, ih);
            if (lvg_rect_contains_point(&item_r, mx, my)) {
                if (r->selected != i) {
                    r->selected = i;
                    if (r->on_change)
                        r->on_change(i, r->on_change_user);
                }
                return 1;
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_radio_init(lui_radio_t *r)
{
    if (!r) return;

    lui_widget_init(&r->widget);
    r->widget.width   = lvg_size_hug(0);
    r->widget.height  = lvg_size_hug(0);
    r->widget.measure = radio_measure;
    r->widget.draw    = radio_draw;
    r->widget.on_event = radio_event;

    r->item_count    = 0;
    r->selected      = -1;
    r->orientation   = LUI_RADIO_COLUMN;
    r->item_spacing  = 20;
    r->circle_diameter = RADIO_CIRCLE_DIAMETER;
    r->inner_radius = RADIO_INNER_RADIUS;
    r->label_gap = RADIO_LABEL_GAP;
    r->border_width = 1;

    r->circle_color   = LVG_COLOR_RGB(0x40, 0x44, 0x4B);
    r->selected_color = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    r->text_color     = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    r->border_color   = LVG_COLOR_RGB(0x6C, 0x70, 0x76);
    r->font           = NULL;

    r->on_change      = NULL;
    r->on_change_user = NULL;
}

int lui_radio_add_item(lui_radio_t *r, const char *label)
{
    if (!r || !label || r->item_count >= LUI_RADIO_MAX_ITEMS)
        return -1;

    int idx = r->item_count;
    int len = (int)strlen(label);
    if (len > LUI_RADIO_MAX_ITEM_LEN) len = LUI_RADIO_MAX_ITEM_LEN;
    memcpy(r->items[idx], label, len);
    r->items[idx][len] = '\0';
    r->item_count++;
    return idx;
}

void lui_radio_set_selected(lui_radio_t *r, int index)
{
    if (!r) return;
    if (index < -1 || index >= r->item_count)
        index = -1;
    r->selected = index;
}
