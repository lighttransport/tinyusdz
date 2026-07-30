/*
 * combo.c — Combo box (dropdown) widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/combo.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int combo_text_padding(const lui_combo_t *cb);
static int combo_arrow_size(const lui_combo_t *cb);
static int combo_arrow_area_width(const lui_combo_t *cb);
static int combo_border_width(const lui_combo_t *cb);

/* Compute the dropdown list rect (below the main button). */
static lvg_rect_t combo_dropdown_rect(const lui_combo_t *cb)
{
    if (!cb || cb->item_count <= 0)
        return lvg_rect_make(0, 0, 0, 0);
    lvg_rect_t r = lui_widget_absolute_rect((lui_widget_t *)&cb->widget);
    int visible = cb->item_count < cb->max_visible
                ? cb->item_count : cb->max_visible;
    if (visible < 1) visible = 1;
    return lvg_rect_make(r.x, r.y + r.height,
                          r.width, visible * cb->item_height + 2);
}

static void combo_draw_dropdown(lui_combo_t *cb, lvg_canvas_t *canvas)
{
    if (!cb || !canvas || !cb->open || cb->item_count <= 0)
        return;

    int cr = cb->corner_radius;
    lvg_rect_t dr = combo_dropdown_rect(cb);
    if (lvg_rect_is_empty(&dr)) return;

    /* Dropdown background */
    lvg_canvas_fill_rounded_rect(canvas, dr.x, dr.y, dr.width, dr.height,
                                  cr, cb->drop_bg);
    lvg_canvas_stroke_rounded_rect(canvas, dr.x, dr.y, dr.width, dr.height,
                                    cr, cb->drop_border,
                                    combo_border_width(cb));

    /* Items */
    int visible = cb->item_count < cb->max_visible
                ? cb->item_count : cb->max_visible;
    for (int i = 0; i < visible; i++) {
        int idx = cb->scroll_offset + i;
        if (idx >= cb->item_count) break;

        int iy = dr.y + 1 + i * cb->item_height;

        /* Hover highlight */
        if (idx == cb->hovered) {
            lvg_canvas_fill_rect(canvas, dr.x + 1, iy,
                                  dr.width - 2, cb->item_height,
                                  cb->drop_hover);
        }

        /* Item text */
        const char *label = cb->items[idx];
        lvg_color_t tc = (idx == cb->selected)
                       ? cb->drop_hover : cb->text_color;
        if (idx == cb->hovered) tc = cb->text_color;
#ifdef LUI_HAVE_FONTS
        if (cb->font) {
            int ty = iy + lui_font_ascent(cb->font) +
                     (cb->item_height - lui_font_line_height(cb->font)) / 2;
            lui_canvas_draw_text(canvas, dr.x + combo_text_padding(cb), ty,
                                 label, -1,
                                 cb->font, tc);
        } else {
#endif
            int label_len = (int)strlen(label);
            int tx = dr.x + combo_text_padding(cb);
            int ty = iy + (cb->item_height - 10) / 2;
            for (int c = 0; c < label_len && tx < dr.x + dr.width - 8; c++) {
                lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, tc);
                tx += 7;
            }
#ifdef LUI_HAVE_FONTS
        }
#endif
    }
}

/* Draw a small down-arrow triangle. */
static void draw_arrow(lvg_canvas_t *canvas, int cx, int cy, int size,
                        lvg_color_t color)
{
    int hs = size / 2;
    lvg_canvas_fill_triangle(canvas,
                              cx - hs, cy - hs / 2,
                              cx + hs, cy - hs / 2,
                              cx,      cy + hs / 2,
                              color);
}

static int combo_text_padding(const lui_combo_t *cb)
{
    return cb->text_padding > 0 ? cb->text_padding : 8;
}

static int combo_arrow_size(const lui_combo_t *cb)
{
    return cb->arrow_size > 0 ? cb->arrow_size : 8;
}

static int combo_arrow_area_width(const lui_combo_t *cb)
{
    return cb->arrow_area_width > 0 ? cb->arrow_area_width : 24;
}

static int combo_border_width(const lui_combo_t *cb)
{
    return cb->border_width > 0 ? cb->border_width : 1;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int combo_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    (void)w; (void)user;
    *out_w = 160;
    *out_h = 28;
    return 0;
}

static void combo_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_combo_t *cb = (lui_combo_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int cr = cb->corner_radius;

    /* Main button background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                  cb->bg_color);

    /* Bevel */
    if (LVG_COLOR_A(cb->bevel_light) > 0 && r.width > 2 && r.height > 2) {
        lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, 1, cb->bevel_light);
        lvg_canvas_fill_rect(canvas, r.x, r.y, 1, r.height, cb->bevel_light);
        lvg_canvas_fill_rect(canvas, r.x, r.y + r.height - 1, r.width, 1,
                              cb->bevel_shadow);
        lvg_canvas_fill_rect(canvas, r.x + r.width - 1, r.y, 1, r.height,
                              cb->bevel_shadow);
    }

    /* Border */
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height, cr,
                                    cb->border_color, combo_border_width(cb));

    /* Selected text */
    if (cb->selected >= 0 && cb->selected < cb->item_count) {
        const char *label = cb->items[cb->selected];
#ifdef LUI_HAVE_FONTS
        if (cb->font) {
            int ty = r.y + lui_font_ascent(cb->font) +
                     (r.height - lui_font_line_height(cb->font)) / 2;
            lui_canvas_draw_text(canvas, r.x + combo_text_padding(cb), ty,
                                 label, -1, cb->font,
                                 cb->text_color);
        } else {
#endif
            int label_len = (int)strlen(label);
            int tx = r.x + combo_text_padding(cb);
            int ty = r.y + (r.height - 10) / 2;
            for (int i = 0; i < label_len &&
                    tx < r.x + r.width - combo_arrow_area_width(cb); i++) {
                lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, cb->text_color);
                tx += 7;
            }
#ifdef LUI_HAVE_FONTS
        }
#endif
    }

    /* Down arrow */
    int arrow_area = combo_arrow_area_width(cb);
    int arrow_cx = r.x + r.width - arrow_area / 2;
    int arrow_cy = r.y + r.height / 2;
    draw_arrow(canvas, arrow_cx, arrow_cy, combo_arrow_size(cb),
               cb->arrow_color);

    /* Separator line before arrow */
    int sep_w = combo_border_width(cb);
    int sep_pad = sep_w * 4;
    lvg_canvas_fill_rect(canvas, r.x + r.width - arrow_area, r.y + sep_pad,
                          sep_w, r.height - 2 * sep_pad, cb->border_color);

    /* Dropdown list (if open) */
    combo_draw_dropdown(cb, canvas);
}

static int combo_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_combo_t *cb = (lui_combo_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        lvg_rect_t r = lui_widget_absolute_rect(w);

        if (cb->open) {
            /* Check if click is in dropdown area */
            lvg_rect_t dr = combo_dropdown_rect(cb);
            if (lvg_rect_contains_point(&dr, mx, my)) {
                int visible = cb->item_count < cb->max_visible
                            ? cb->item_count : cb->max_visible;
                int rel_y = my - (dr.y + 1);
                int idx = cb->scroll_offset + rel_y / cb->item_height;
                if (idx >= 0 && idx < cb->item_count && idx < cb->scroll_offset + visible) {
                    int old = cb->selected;
                    cb->selected = idx;
                    cb->open = false;
                    cb->hovered = -1;
                    if (idx != old && cb->on_change)
                        cb->on_change(idx, cb->items[idx], cb->on_change_user);
                }
                return 1;
            }

            /* Click on main button while open — close */
            if (lvg_rect_contains_point(&r, mx, my)) {
                cb->open = false;
                cb->hovered = -1;
                return 1;
            }

            /* Click outside — close */
            cb->open = false;
            cb->hovered = -1;
            return 0;
        }

        /* Closed: click on button opens */
        if (lvg_rect_contains_point(&r, mx, my)) {
            cb->open = true;
            cb->hovered = -1;
            cb->scroll_offset = 0;
            return 1;
        }
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && cb->open) {
        lvg_rect_t dr = combo_dropdown_rect(cb);
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        if (lvg_rect_contains_point(&dr, mx, my)) {
            int rel_y = my - (dr.y + 1);
            cb->hovered = cb->scroll_offset + rel_y / cb->item_height;
            if (cb->hovered >= cb->item_count) cb->hovered = -1;
        } else {
            cb->hovered = -1;
        }
        return 1;
    }

    if (event->type == LUI_EVENT_SCROLL && cb->open) {
        int delta = event->data.scroll.delta_y > 0 ? -1 : 1;
        int max_scroll = cb->item_count - cb->max_visible;
        if (max_scroll < 0) max_scroll = 0;
        cb->scroll_offset += delta;
        if (cb->scroll_offset < 0) cb->scroll_offset = 0;
        if (cb->scroll_offset > max_scroll) cb->scroll_offset = max_scroll;
        return 1;
    }

    if (event->type == LUI_EVENT_KEY_DOWN && cb->open) {
        switch (event->data.key.key) {
        case LUI_KEY_ESCAPE:
            cb->open = false;
            cb->hovered = -1;
            return 1;
        case LUI_KEY_RETURN:
            if (cb->hovered >= 0 && cb->hovered < cb->item_count) {
                int old = cb->selected;
                cb->selected = cb->hovered;
                cb->open = false;
                cb->hovered = -1;
                if (cb->selected != old && cb->on_change)
                    cb->on_change(cb->selected, cb->items[cb->selected],
                                  cb->on_change_user);
            }
            return 1;
        case LUI_KEY_UP:
            if (cb->hovered > 0) cb->hovered--;
            else if (cb->hovered < 0 && cb->item_count > 0)
                cb->hovered = 0;
            /* Scroll into view */
            if (cb->hovered < cb->scroll_offset)
                cb->scroll_offset = cb->hovered;
            return 1;
        case LUI_KEY_DOWN:
            if (cb->hovered < cb->item_count - 1) cb->hovered++;
            else if (cb->hovered < 0 && cb->item_count > 0)
                cb->hovered = 0;
            /* Scroll into view */
            if (cb->hovered >= cb->scroll_offset + cb->max_visible)
                cb->scroll_offset = cb->hovered - cb->max_visible + 1;
            return 1;
        default:
            break;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_combo_init(lui_combo_t *cb)
{
    if (!cb) return;

    lui_widget_init(&cb->widget);
    cb->widget.width   = lvg_size_hug(160);
    cb->widget.height  = lvg_size_hug(0);
    cb->widget.measure = combo_measure;
    cb->widget.draw    = combo_draw;
    cb->widget.on_event = combo_event;
    cb->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    cb->item_count    = 0;
    cb->selected      = -1;
    cb->open          = false;
    cb->hovered       = -1;
    cb->max_visible   = 8;
    cb->scroll_offset = 0;

    cb->item_height   = 24;
    cb->corner_radius = 3;
    cb->text_padding  = 8;
    cb->arrow_size    = 8;
    cb->arrow_area_width = 24;
    cb->border_width  = 1;
    cb->bg_color      = LVG_COLOR_RGB(0x3A, 0x3D, 0x42);
    cb->border_color  = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    cb->text_color    = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    cb->arrow_color   = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    cb->drop_bg       = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    cb->drop_hover    = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    cb->drop_border   = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    cb->bevel_light   = LVG_COLOR_TRANSPARENT;
    cb->bevel_shadow  = LVG_COLOR_TRANSPARENT;
    cb->font          = NULL;

    cb->on_change      = NULL;
    cb->on_change_user = NULL;
}

int lui_combo_add_item(lui_combo_t *cb, const char *label)
{
    if (!cb || !label || cb->item_count >= LUI_COMBO_MAX_ITEMS)
        return -1;

    int idx = cb->item_count;
    int len = (int)strlen(label);
    if (len > LUI_COMBO_MAX_ITEM_LEN) len = LUI_COMBO_MAX_ITEM_LEN;
    memcpy(cb->items[idx], label, len);
    cb->items[idx][len] = '\0';
    cb->item_count++;
    return idx;
}

void lui_combo_clear(lui_combo_t *cb)
{
    if (!cb) return;
    cb->item_count = 0;
    cb->selected = -1;
    cb->open = false;
    cb->hovered = -1;
    cb->scroll_offset = 0;
}

void lui_combo_set_selected(lui_combo_t *cb, int index)
{
    if (!cb) return;
    if (index < -1 || index >= cb->item_count)
        index = -1;
    cb->selected = index;
}

const char *lui_combo_selected_text(const lui_combo_t *cb)
{
    if (!cb || cb->selected < 0 || cb->selected >= cb->item_count)
        return NULL;
    return cb->items[cb->selected];
}

lvg_rect_t lui_combo_dropdown_rect(const lui_combo_t *cb)
{
    return combo_dropdown_rect(cb);
}

void lui_combo_draw_dropdown(lui_combo_t *cb, lvg_canvas_t *canvas)
{
    combo_draw_dropdown(cb, canvas);
}
