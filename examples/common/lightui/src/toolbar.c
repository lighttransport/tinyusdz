/*
 * toolbar.c — Toolbar widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/toolbar.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int find_item_idx(const lui_toolbar_t *tb, int item_id)
{
    for (int i = 0; i < tb->item_count; i++)
        if (tb->items[i].id == item_id) return i;
    return -1;
}

/* Get the rect for item at index, relative to widget origin. */
static lvg_rect_t item_rect(const lui_toolbar_t *tb, int index,
                              lvg_rect_t wr)
{
    int pos = 0;
    for (int i = 0; i < index && i < tb->item_count; i++) {
        if (tb->items[i].type == LUI_TB_SEPARATOR)
            pos += tb->separator_size + tb->spacing;
        else
            pos += tb->button_size + tb->spacing;
    }

    int sz;
    if (tb->items[index].type == LUI_TB_SEPARATOR)
        sz = tb->separator_size;
    else
        sz = tb->button_size;

    if (tb->vertical)
        return lvg_rect_make(wr.x, wr.y + pos, wr.width, sz);
    else
        return lvg_rect_make(wr.x + pos, wr.y, sz, wr.height);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int tb_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_toolbar_t *tb = (const lui_toolbar_t *)w;
    (void)user;

    int total = 0;
    for (int i = 0; i < tb->item_count; i++) {
        if (tb->items[i].type == LUI_TB_SEPARATOR)
            total += tb->separator_size;
        else
            total += tb->button_size;
        if (i < tb->item_count - 1) total += tb->spacing;
    }

    if (tb->vertical) {
        *out_w = tb->button_size;
        *out_h = total;
    } else {
        *out_w = total;
        *out_h = tb->button_size;
    }
    return 0;
}

static void tb_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_toolbar_t *tb = (lui_toolbar_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, wr.height, tb->bg);

    for (int i = 0; i < tb->item_count; i++) {
        lui_tb_item_t *item = &tb->items[i];
        lvg_rect_t ir = item_rect(tb, i, wr);

        if (item->type == LUI_TB_SEPARATOR) {
            /* Draw separator line */
            if (tb->vertical) {
                int sy = ir.y + ir.height / 2;
                lvg_canvas_fill_rect(canvas, ir.x + 4, sy,
                                      ir.width - 8, 1, tb->separator_color);
            } else {
                int sx = ir.x + ir.width / 2;
                lvg_canvas_fill_rect(canvas, sx, ir.y + 4,
                                      1, ir.height - 8, tb->separator_color);
            }
            continue;
        }

        /* Button background */
        lvg_color_t bg;
        if (!item->enabled) {
            bg = tb->button_bg;
        } else if (item->id == tb->pressed_id) {
            bg = tb->button_pressed;
        } else if (item->active) {
            bg = tb->button_active;
        } else if (item->id == tb->hovered_id) {
            bg = tb->button_hover;
        } else {
            bg = tb->button_bg;
        }

        lvg_canvas_fill_rounded_rect(canvas, ir.x, ir.y, ir.width, ir.height,
                                      tb->corner_radius, bg);

        /* Icon indicator (color square in center) */
        lvg_color_t icon = item->enabled ? item->icon_color : tb->disabled_color;
        if (LVG_COLOR_A(icon) == 0)
            icon = item->enabled ? tb->icon_default : tb->disabled_color;
        int icon_sz = tb->button_size / 2;
        int ix = ir.x + (ir.width - icon_sz) / 2;
        int iy = ir.y + (ir.height - icon_sz) / 2;
        lvg_canvas_fill_rect(canvas, ix, iy, icon_sz, icon_sz, icon);

        /* Label text below icon (if space) */
        int label_len = (int)strlen(item->label);
        if (label_len > 0 && !tb->vertical && ir.height >= tb->button_size + 12) {
            int tx = ir.x + (ir.width - label_len * 5) / 2;
            int ty = iy + icon_sz + 2;
            lvg_color_t tc = item->enabled ? tb->icon_default : tb->disabled_color;
            for (int c = 0; c < label_len && c < 4; c++)
                lvg_canvas_fill_rect(canvas, tx + c * 6, ty, 4, 6, tc);
        }
    }
}

static int tb_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_toolbar_t *tb = (lui_toolbar_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        tb->hovered_id = -1;
        for (int i = 0; i < tb->item_count; i++) {
            if (tb->items[i].type == LUI_TB_SEPARATOR) continue;
            lvg_rect_t ir = item_rect(tb, i, wr);
            if (lvg_rect_contains_point(&ir, mx, my)) {
                tb->hovered_id = tb->items[i].id;
                break;
            }
        }
        return tb->hovered_id >= 0 ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        for (int i = 0; i < tb->item_count; i++) {
            if (tb->items[i].type == LUI_TB_SEPARATOR) continue;
            if (!tb->items[i].enabled) continue;
            lvg_rect_t ir = item_rect(tb, i, wr);
            if (lvg_rect_contains_point(&ir, mx, my)) {
                tb->pressed_id = tb->items[i].id;
                return 1;
            }
        }
        return 0;
    }

    if (event->type == LUI_EVENT_MOUSE_UP &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        if (tb->pressed_id < 0) return 0;

        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        int pressed = tb->pressed_id;
        tb->pressed_id = -1;

        int idx = find_item_idx(tb, pressed);
        if (idx < 0) return 0;

        lvg_rect_t ir = item_rect(tb, idx, wr);
        if (!lvg_rect_contains_point(&ir, mx, my)) return 1;

        lui_tb_item_t *item = &tb->items[idx];

        if (item->type == LUI_TB_TOGGLE) {
            item->active = !item->active;
        } else if (item->type == LUI_TB_RADIO) {
            /* Deactivate others in same group */
            for (int j = 0; j < tb->item_count; j++) {
                if (tb->items[j].type == LUI_TB_RADIO &&
                    tb->items[j].group == item->group)
                    tb->items[j].active = false;
            }
            item->active = true;
        }

        if (tb->on_click)
            tb->on_click(item->id, item->active, tb->on_click_user);

        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_toolbar_init(lui_toolbar_t *tb)
{
    if (!tb) return;

    lui_widget_init(&tb->widget);
    tb->widget.width   = lvg_size_hug(0);
    tb->widget.height  = lvg_size_hug(0);
    tb->widget.measure = tb_measure;
    tb->widget.draw    = tb_draw;
    tb->widget.on_event = tb_event;

    tb->item_count     = 0;
    tb->next_id        = 1;
    tb->vertical       = false;
    tb->button_size    = 28;
    tb->spacing        = 2;
    tb->separator_size = 8;
    tb->hovered_id     = -1;
    tb->pressed_id     = -1;

    tb->bg              = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    tb->button_bg       = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    tb->button_hover    = LVG_COLOR_RGB(0x3A, 0x3E, 0x46);
    tb->button_active   = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    tb->button_pressed  = LVG_COLOR_RGB(0x36, 0x58, 0x80);
    tb->border_color    = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    tb->icon_default    = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    tb->disabled_color  = LVG_COLOR_RGB(0x60, 0x64, 0x68);
    tb->separator_color = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    tb->corner_radius   = 3;

    tb->on_click      = NULL;
    tb->on_click_user = NULL;
}

int lui_toolbar_add_item(lui_toolbar_t *tb, lui_tb_item_type_t type,
                          const char *label, int group)
{
    if (!tb || tb->item_count >= LUI_TOOLBAR_MAX_ITEMS) return -1;

    lui_tb_item_t *item = &tb->items[tb->item_count];
    item->id = tb->next_id++;
    item->type = type;
    item->icon_color = 0;
    item->active = false;
    item->enabled = true;
    item->group = group;

    if (label) {
        int len = (int)strlen(label);
        if (len > LUI_TOOLBAR_MAX_LABEL) len = LUI_TOOLBAR_MAX_LABEL;
        memcpy(item->label, label, len);
        item->label[len] = '\0';
    } else {
        item->label[0] = '\0';
    }

    tb->item_count++;
    return item->id;
}

int lui_toolbar_add_separator(lui_toolbar_t *tb)
{
    return lui_toolbar_add_item(tb, LUI_TB_SEPARATOR, NULL, 0);
}

void lui_toolbar_set_active(lui_toolbar_t *tb, int item_id, bool active)
{
    if (!tb) return;
    int idx = find_item_idx(tb, item_id);
    if (idx < 0) return;

    lui_tb_item_t *item = &tb->items[idx];

    if (item->type == LUI_TB_RADIO && active) {
        /* Deactivate others in group */
        for (int j = 0; j < tb->item_count; j++) {
            if (tb->items[j].type == LUI_TB_RADIO &&
                tb->items[j].group == item->group)
                tb->items[j].active = false;
        }
    }
    item->active = active;
}

void lui_toolbar_set_enabled(lui_toolbar_t *tb, int item_id, bool enabled)
{
    if (!tb) return;
    int idx = find_item_idx(tb, item_id);
    if (idx >= 0) tb->items[idx].enabled = enabled;
}

lui_tb_item_t *lui_toolbar_get_item(lui_toolbar_t *tb, int item_id)
{
    if (!tb) return NULL;
    int idx = find_item_idx(tb, item_id);
    return idx >= 0 ? &tb->items[idx] : NULL;
}
