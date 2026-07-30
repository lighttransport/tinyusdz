/*
 * draglist.c — Reorderable drag list widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/draglist.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void draglist_move_item(lui_draglist_t *dl, int from, int to)
{
    if (from == to || from < 0 || to < 0 ||
        from >= dl->count || to >= dl->count)
        return;

    lui_draglist_item_t tmp;
    memcpy(&tmp, &dl->items[from], sizeof(tmp));

    if (from < to) {
        memmove(&dl->items[from], &dl->items[from + 1],
                (to - from) * sizeof(lui_draglist_item_t));
    } else {
        memmove(&dl->items[to + 1], &dl->items[to],
                (from - to) * sizeof(lui_draglist_item_t));
    }
    memcpy(&dl->items[to], &tmp, sizeof(tmp));

    /* Update selected index to follow the moved item */
    if (dl->selected == from) {
        dl->selected = to;
    } else if (from < to) {
        if (dl->selected > from && dl->selected <= to)
            dl->selected--;
    } else {
        if (dl->selected >= to && dl->selected < from)
            dl->selected++;
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int draglist_measure(const lui_widget_t *w, int *out_w, int *out_h,
                             void *user)
{
    const lui_draglist_t *dl = (const lui_draglist_t *)w;
    (void)user;
    *out_w = 200;
    *out_h = dl->count * dl->item_height;
    return 0;
}

static void draglist_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_draglist_t *dl = (lui_draglist_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, dl->bg_color);

    /* Draw items */
    for (int i = 0; i < dl->count; i++) {
        /* Skip the dragged item in its original position */
        if (i == dl->drag_index) continue;

        int iy = r.y + i * dl->item_height;
        lvg_color_t bg = dl->item_bg;
        if (i == dl->selected)
            bg = dl->item_selected_bg;

        lvg_canvas_fill_rect(canvas, r.x, iy, r.width, dl->item_height, bg);

        /* Border between items */
        lvg_canvas_fill_rect(canvas, r.x, iy + dl->item_height - 1,
                              r.width, 1, dl->border_color);

        /* Item text */
        const char *label = dl->items[i].label;
        int label_len = (int)strlen(label);
        int tx = r.x + 8;
        int ty = iy + (dl->item_height - 10) / 2;
        for (int c = 0; c < label_len && tx < r.x + r.width - 8; c++) {
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, dl->text_color);
            tx += 7;
        }
    }

    /* Draw insertion indicator during drag */
    if (dl->drag_index >= 0 && dl->insert_index >= 0) {
        int iy = r.y + dl->insert_index * dl->item_height;
        lvg_canvas_fill_rect(canvas, r.x, iy - 1, r.width, 2,
                              dl->drag_indicator_color);
    }

    /* Draw the dragged item at the mouse position with slight transparency */
    if (dl->drag_index >= 0 && dl->drag_index < dl->count) {
        int drag_item_y = dl->drag_y - dl->item_height / 2;
        lvg_color_t drag_bg = LVG_COLOR_ARGB(0xCC,
            LVG_COLOR_R(dl->item_selected_bg),
            LVG_COLOR_G(dl->item_selected_bg),
            LVG_COLOR_B(dl->item_selected_bg));

        lvg_canvas_fill_rect(canvas, r.x + 2, drag_item_y,
                              r.width - 4, dl->item_height, drag_bg);

        const char *label = dl->items[dl->drag_index].label;
        int label_len = (int)strlen(label);
        int tx = r.x + 10;
        int ty = drag_item_y + (dl->item_height - 10) / 2;
        for (int c = 0; c < label_len && tx < r.x + r.width - 8; c++) {
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, dl->text_color);
            tx += 7;
        }
    }
}

static int draglist_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_draglist_t *dl = (lui_draglist_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            int mx = event->data.mouse_button.x;
            int my = event->data.mouse_button.y;
            if (lvg_rect_contains_point(&r, mx, my)) {
                int idx = (my - r.y) / dl->item_height;
                if (idx >= 0 && idx < dl->count) {
                    dl->selected = idx;
                    dl->drag_index = idx;
                    dl->drag_y = my;
                    dl->drag_start_y = my;
                    dl->insert_index = idx;
                    return 1;
                }
            }
        }
        break;

    case LUI_EVENT_MOUSE_MOVE:
        if (dl->drag_index >= 0) {
            int my = event->data.mouse_move.y;
            dl->drag_y = my;

            /* Compute insertion index */
            int rel_y = my - r.y;
            int idx = (rel_y + dl->item_height / 2) / dl->item_height;
            if (idx < 0) idx = 0;
            if (idx > dl->count) idx = dl->count;
            dl->insert_index = idx;
            return 1;
        }
        break;

    case LUI_EVENT_MOUSE_UP:
        if (event->data.mouse_button.button == LUI_MOUSE_LEFT &&
            dl->drag_index >= 0) {
            int from = dl->drag_index;
            int to = dl->insert_index;

            /* Adjust target: if inserting after the dragged item's
             * original position, account for removal shifting indices. */
            if (to > from) to--;
            if (to < 0) to = 0;
            if (to >= dl->count) to = dl->count - 1;

            dl->drag_index = -1;
            dl->insert_index = -1;

            if (from != to) {
                draglist_move_item(dl, from, to);
                if (dl->on_reorder)
                    dl->on_reorder(from, to, dl->on_reorder_user);
            }
            return 1;
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

void lui_draglist_init(lui_draglist_t *dl)
{
    if (!dl) return;

    lui_widget_init(&dl->widget);
    dl->widget.width    = lvg_size_hug(200);
    dl->widget.height   = lvg_size_hug(0);
    dl->widget.measure  = draglist_measure;
    dl->widget.draw     = draglist_draw;
    dl->widget.on_event = draglist_event;

    dl->count        = 0;
    dl->selected     = -1;
    dl->drag_index   = -1;
    dl->drag_y       = 0;
    dl->drag_start_y = 0;
    dl->insert_index = -1;
    dl->item_height  = 28;

    dl->bg_color             = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    dl->item_bg              = LVG_COLOR_RGB(0x33, 0x36, 0x3B);
    dl->item_selected_bg     = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    dl->item_hover_bg        = LVG_COLOR_RGB(0x3A, 0x3D, 0x42);
    dl->text_color           = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    dl->border_color         = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    dl->drag_indicator_color = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);

    dl->on_reorder      = NULL;
    dl->on_reorder_user = NULL;
}

int lui_draglist_add_item(lui_draglist_t *dl, const char *label)
{
    if (!dl || !label || dl->count >= LUI_DRAGLIST_MAX)
        return -1;

    int idx = dl->count;
    int len = (int)strlen(label);
    if (len > 63) len = 63;
    memcpy(dl->items[idx].label, label, len);
    dl->items[idx].label[len] = '\0';
    dl->count++;
    return idx;
}

int lui_draglist_remove_item(lui_draglist_t *dl, int index)
{
    if (!dl || index < 0 || index >= dl->count)
        return -1;

    /* Shift items down */
    for (int i = index; i < dl->count - 1; i++) {
        memcpy(&dl->items[i], &dl->items[i + 1], sizeof(lui_draglist_item_t));
    }
    dl->count--;

    /* Adjust selected */
    if (dl->selected == index)
        dl->selected = -1;
    else if (dl->selected > index)
        dl->selected--;

    return 0;
}

void lui_draglist_clear(lui_draglist_t *dl)
{
    if (!dl) return;
    dl->count = 0;
    dl->selected = -1;
    dl->drag_index = -1;
    dl->insert_index = -1;
}
