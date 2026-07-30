/*
 * layerstack.c -- Layer stack panel widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/layerstack.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline int ls_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Draw a string as 5x10 filled rectangles with 7px advance. */
static void ls_draw_text(lvg_canvas_t *canvas, int x, int y,
                         const char *text, lvg_color_t color, int max_x)
{
    if (!text) return;
    for (int i = 0; text[i] && x + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int layerstack_measure(const lui_widget_t *w, int *out_w, int *out_h,
                              void *user)
{
    const lui_layerstack_t *ls = (const lui_layerstack_t *)w;
    (void)user;
    *out_w = 200;
    *out_h = ls->layer_count * ls->item_height;
    return 0;
}

static void layerstack_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_layerstack_t *ls = (lui_layerstack_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, ls->bg_color);

    /* Draw layers from top to bottom (last layer = bottom of stack, drawn first
     * at the bottom of the visual list). We display layer[count-1] at top. */
    for (int vi = 0; vi < ls->layer_count; vi++) {
        /* Visual row vi maps to layer index (count - 1 - vi) so that
         * the topmost layer in the stack appears at the top of the list. */
        int li = ls->layer_count - 1 - vi;
        int row_y = r.y + vi * ls->item_height - ls->scroll_offset;

        if (row_y + ls->item_height < r.y || row_y > r.y + r.height)
            continue;

        lui_layer_t *layer = &ls->layers[li];

        /* Row background */
        lvg_color_t row_bg = (li == ls->active_layer)
                           ? ls->item_selected_bg : ls->item_bg;
        lvg_canvas_fill_rect(canvas, r.x, row_y, r.width, ls->item_height,
                              row_bg);

        int cx = r.x + 4;
        int cy = row_y + ls->item_height / 2;

        /* Eye icon: filled circle if visible, stroke circle if hidden */
        if (layer->visible) {
            lvg_canvas_fill_circle(canvas, cx + 6, cy, 5, ls->eye_color);
        } else {
            lvg_canvas_stroke_circle(canvas, cx + 6, cy, 5,
                                      ls->eye_color, 1);
        }
        cx += 18;

        /* Lock icon: filled rect if locked, stroke rect if unlocked */
        if (layer->locked) {
            lvg_canvas_fill_rect(canvas, cx, cy - 5, 10, 10,
                                  ls->lock_color);
        } else {
            lvg_canvas_stroke_rect(canvas, cx, cy - 5, 10, 10,
                                    ls->lock_color, 1);
        }
        cx += 16;

        /* Thumbnail swatch */
        lvg_canvas_fill_rect(canvas, cx, row_y + 4,
                              ls->item_height - 8, ls->item_height - 8,
                              layer->thumbnail_color);
        lvg_canvas_stroke_rect(canvas, cx, row_y + 4,
                                ls->item_height - 8, ls->item_height - 8,
                                ls->border_color, 1);
        cx += ls->item_height - 8 + 4;

        /* Name text */
        int text_y = row_y + (ls->item_height - 10) / 2;
        ls_draw_text(canvas, cx, text_y, layer->name, ls->text_color,
                     r.x + r.width - 40);

        /* Opacity bar (small horizontal bar at the bottom of the row) */
        {
            int bar_x = cx;
            int bar_y = row_y + ls->item_height - 6;
            int bar_w = r.x + r.width - 8 - bar_x;
            if (bar_w > 0) {
                lvg_canvas_fill_rect(canvas, bar_x, bar_y, bar_w, 3,
                                      ls->border_color);
                int fill_w = (int)(layer->opacity * (float)bar_w);
                if (fill_w > 0) {
                    lvg_canvas_fill_rect(canvas, bar_x, bar_y, fill_w, 3,
                                          ls->eye_color);
                }
            }
        }

        /* Row separator */
        lvg_canvas_fill_rect(canvas, r.x, row_y + ls->item_height - 1,
                              r.width, 1, ls->border_color);
    }

    /* Outer border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            ls->border_color, 1);
}

static int layerstack_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_layerstack_t *ls = (lui_layerstack_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&r, mx, my)) break;

        int vi = (my - r.y + ls->scroll_offset) / ls->item_height;
        if (vi < 0 || vi >= ls->layer_count) break;

        int li = ls->layer_count - 1 - vi;
        int local_x = mx - r.x;

        /* Eye icon zone: x 4..16 */
        if (local_x >= 4 && local_x < 22) {
            ls->layers[li].visible = !ls->layers[li].visible;
            if (ls->on_change)
                ls->on_change(li, LUI_LAYER_TOGGLE_VIS, ls->on_change_user);
            return 1;
        }

        /* Lock icon zone: x 22..38 */
        if (local_x >= 22 && local_x < 38) {
            ls->layers[li].locked = !ls->layers[li].locked;
            if (ls->on_change)
                ls->on_change(li, LUI_LAYER_TOGGLE_LOCK, ls->on_change_user);
            return 1;
        }

        /* Otherwise select layer */
        ls->active_layer = li;
        for (int i = 0; i < ls->layer_count; i++)
            ls->layers[i].selected = (i == li);
        if (ls->on_change)
            ls->on_change(li, LUI_LAYER_SELECT, ls->on_change_user);
        return 1;
    }

    case LUI_EVENT_SCROLL: {
        int sx = event->data.scroll.x;
        int sy = event->data.scroll.y;
        if (!lvg_rect_contains_point(&r, sx, sy)) break;
        ls->scroll_offset -= (int)(event->data.scroll.delta_y * 20.0f);
        int max_scroll = ls->layer_count * ls->item_height - r.height;
        if (max_scroll < 0) max_scroll = 0;
        ls->scroll_offset = ls_clampi(ls->scroll_offset, 0, max_scroll);
        return 1;
    }

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_layerstack_init(lui_layerstack_t *ls)
{
    if (!ls) return;

    lui_widget_init(&ls->widget);
    ls->widget.width    = lvg_size_hug(200);
    ls->widget.height   = lvg_size_hug(0);
    ls->widget.measure  = layerstack_measure;
    ls->widget.draw     = layerstack_draw;
    ls->widget.on_event = layerstack_event;

    ls->layer_count   = 0;
    ls->active_layer  = -1;
    ls->item_height   = 36;
    ls->scroll_offset = 0;

    ls->bg_color         = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    ls->item_bg          = LVG_COLOR_RGB(0x28, 0x28, 0x3C);
    ls->item_selected_bg = LVG_COLOR_RGB(0x45, 0x47, 0x5A);
    ls->text_color       = LVG_COLOR_RGB(0xCD, 0xD6, 0xF4);
    ls->border_color     = LVG_COLOR_RGB(0x58, 0x5B, 0x70);
    ls->eye_color        = LVG_COLOR_RGB(0xA6, 0xE3, 0xA1);
    ls->lock_color       = LVG_COLOR_RGB(0xF9, 0xE2, 0xAF);

    ls->on_change      = NULL;
    ls->on_change_user = NULL;
}

int lui_layerstack_add(lui_layerstack_t *ls, const char *name)
{
    if (!ls || ls->layer_count >= LUI_LAYERSTACK_MAX) return -1;
    int idx = ls->layer_count++;
    lui_layer_t *layer = &ls->layers[idx];
    memset(layer, 0, sizeof(*layer));
    if (name) { strncpy(layer->name, name, sizeof(layer->name) - 1); }
    layer->visible         = true;
    layer->locked          = false;
    layer->opacity         = 1.0f;
    layer->blend_mode      = LUI_BLEND_NORMAL;
    layer->selected        = false;
    layer->thumbnail_color = LVG_COLOR_RGB(0x80, 0x80, 0x80);
    return idx;
}

void lui_layerstack_remove(lui_layerstack_t *ls, int index)
{
    if (!ls || index < 0 || index >= ls->layer_count) return;
    for (int i = index; i < ls->layer_count - 1; i++)
        ls->layers[i] = ls->layers[i + 1];
    ls->layer_count--;
    if (ls->active_layer == index) ls->active_layer = -1;
    else if (ls->active_layer > index) ls->active_layer--;
}

void lui_layerstack_set_active(lui_layerstack_t *ls, int index)
{
    if (!ls) return;
    if (index < 0 || index >= ls->layer_count) {
        ls->active_layer = -1;
        return;
    }
    ls->active_layer = index;
    for (int i = 0; i < ls->layer_count; i++)
        ls->layers[i].selected = (i == index);
}

void lui_layerstack_move(lui_layerstack_t *ls, int from, int to)
{
    if (!ls || from < 0 || from >= ls->layer_count) return;
    if (to < 0) to = 0;
    if (to >= ls->layer_count) to = ls->layer_count - 1;
    if (from == to) return;

    lui_layer_t tmp = ls->layers[from];
    if (from < to) {
        for (int i = from; i < to; i++)
            ls->layers[i] = ls->layers[i + 1];
    } else {
        for (int i = from; i > to; i--)
            ls->layers[i] = ls->layers[i - 1];
    }
    ls->layers[to] = tmp;

    /* Update active layer tracking */
    if (ls->active_layer == from)
        ls->active_layer = to;
    else if (from < to && ls->active_layer > from && ls->active_layer <= to)
        ls->active_layer--;
    else if (from > to && ls->active_layer >= to && ls->active_layer < from)
        ls->active_layer++;

    if (ls->on_change)
        ls->on_change(to, LUI_LAYER_REORDER, ls->on_change_user);
}
