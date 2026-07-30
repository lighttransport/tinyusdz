/*
 * menu.c — Context menu / popup menu widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/menu.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Compute the total height of the menu based on items. */
static int menu_total_height(const lui_menu_t *menu)
{
    int h = 2; /* top + bottom border padding */
    for (int i = 0; i < menu->item_count; i++) {
        if (menu->items[i].type == LUI_MENU_ITEM_SEPARATOR)
            h += menu->separator_height;
        else
            h += menu->item_height;
    }
    return h;
}

/* Compute the width of the menu based on the widest item. */
static int menu_total_width(const lui_menu_t *menu)
{
    int max_label = 0;
    int max_shortcut = 0;
    for (int i = 0; i < menu->item_count; i++) {
        if (menu->items[i].type == LUI_MENU_ITEM_SEPARATOR)
            continue;
#ifdef LUI_HAVE_FONTS
        int ll = menu->font
               ? lui_font_measure_text(menu->font, menu->items[i].label, -1)
               : (int)strlen(menu->items[i].label) * 7;
#else
        int ll = (int)strlen(menu->items[i].label) * 7;
#endif
        if (ll > max_label) max_label = ll;
#ifdef LUI_HAVE_FONTS
        int sl = menu->font
               ? lui_font_measure_text(menu->font, menu->items[i].shortcut, -1)
               : (int)strlen(menu->items[i].shortcut) * 7;
#else
        int sl = (int)strlen(menu->items[i].shortcut) * 7;
#endif
        if (sl > max_shortcut) max_shortcut = sl;
    }
    /* checkmark area (14) + label text + gap + shortcut text + submenu arrow (14) + padding */
    int w = menu->padding_x + 14 + max_label + menu->padding_x;
    if (max_shortcut > 0)
        w += 12 + max_shortcut; /* gap + shortcut */
    w += 14 + menu->padding_x; /* submenu arrow area + right padding */
    if (w < 100) w = 100;
    return w;
}

/* Get the menu popup rect in absolute coordinates. */
static lvg_rect_t menu_popup_rect(const lui_menu_t *menu)
{
    return lvg_rect_make(menu->popup_x, menu->popup_y,
                         menu_total_width(menu), menu_total_height(menu));
}

/* Get the Y offset and height for item at @index within the menu rect. */
static void menu_item_geometry(const lui_menu_t *menu, int index,
                               int *out_y, int *out_h)
{
    int y = 1; /* top border padding */
    for (int i = 0; i < menu->item_count; i++) {
        int ih = (menu->items[i].type == LUI_MENU_ITEM_SEPARATOR)
               ? menu->separator_height : menu->item_height;
        if (i == index) {
            *out_y = y;
            *out_h = ih;
            return;
        }
        y += ih;
    }
    *out_y = 0;
    *out_h = 0;
}

/* Hit-test: which item index is at position (mx, my) in absolute coords?
 * Returns -1 if none. */
static int menu_hit_item(const lui_menu_t *menu, int mx, int my)
{
    lvg_rect_t mr = menu_popup_rect(menu);
    if (!lvg_rect_contains_point(&mr, mx, my))
        return -1;

    int rel_y = my - mr.y;
    int y = 1;
    for (int i = 0; i < menu->item_count; i++) {
        int ih = (menu->items[i].type == LUI_MENU_ITEM_SEPARATOR)
               ? menu->separator_height : menu->item_height;
        if (rel_y >= y && rel_y < y + ih)
            return i;
        y += ih;
    }
    return -1;
}

/* Draw text as character rectangles: 5x10 px per char, 7px spacing. */
static void draw_text_rects(lvg_canvas_t *canvas, int x, int y,
                            const char *text, int max_x, lvg_color_t color)
{
    int len = (int)strlen(text);
    int tx = x;
    for (int i = 0; i < len && tx + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, tx, y, 5, 10, color);
        tx += 7;
    }
}

/* Draw a small right-pointing triangle (submenu indicator). */
static void draw_right_arrow(lvg_canvas_t *canvas, int cx, int cy,
                             int size, lvg_color_t color)
{
    int hs = size / 2;
    lvg_canvas_fill_triangle(canvas,
                             cx - hs / 2, cy - hs,
                             cx + hs / 2, cy,
                             cx - hs / 2, cy + hs,
                             color);
}

/* Draw a checkmark as two small line segments. */
static void draw_checkmark(lvg_canvas_t *canvas, int x, int y,
                           lvg_color_t color)
{
    /* Simple checkmark: small V shape using filled rects */
    lvg_canvas_draw_line(canvas, x, y + 4, x + 3, y + 7, color, 1);
    lvg_canvas_draw_line(canvas, x + 3, y + 7, x + 7, y, color, 1);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int menu_measure(const lui_widget_t *w, int *out_w, int *out_h,
                        void *user)
{
    const lui_menu_t *menu = (const lui_menu_t *)w;
    (void)user;
    if (menu->open) {
        *out_w = menu_total_width(menu);
        *out_h = menu_total_height(menu);
    } else {
        *out_w = 0;
        *out_h = 0;
    }
    return 0;
}

static void menu_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_menu_t *menu = (lui_menu_t *)w;
    if (!menu->open || menu->item_count == 0)
        return;

    lvg_rect_t mr = menu_popup_rect(menu);
    int mw = mr.width;

    /* Background */
    lvg_canvas_fill_rect(canvas, mr.x, mr.y, mw, mr.height, menu->bg_color);

    /* Border */
    lvg_canvas_stroke_rect(canvas, mr.x, mr.y, mw, mr.height,
                           menu->border_color, 1);

    /* Items */
    for (int i = 0; i < menu->item_count; i++) {
        const lui_menu_item_t *item = &menu->items[i];
        int iy, ih;
        menu_item_geometry(menu, i, &iy, &ih);
        int abs_y = mr.y + iy;

        if (item->type == LUI_MENU_ITEM_SEPARATOR) {
            /* Horizontal separator line */
            int line_y = abs_y + ih / 2;
            lvg_canvas_fill_rect(canvas, mr.x + 4, line_y,
                                 mw - 8, 1, menu->separator_color);
            continue;
        }

        /* Hover highlight (only for enabled items) */
        if (i == menu->hovered && item->enabled) {
            lvg_canvas_fill_rect(canvas, mr.x + 1, abs_y,
                                 mw - 2, ih, menu->hover_color);
        }

        lvg_color_t tc = item->enabled ? menu->text_color
                                       : menu->disabled_color;

        /* Checkmark for checked checkbox items */
        if (item->type == LUI_MENU_ITEM_CHECKBOX && item->checked) {
            draw_checkmark(canvas,
                           mr.x + menu->padding_x,
                           abs_y + (ih - 10) / 2,
                           tc);
        }

        /* Label text */
        int label_x = mr.x + menu->padding_x + 14;
#ifdef LUI_HAVE_FONTS
        int text_y = menu->font
                   ? abs_y + lui_font_ascent(menu->font) +
                     (ih - lui_font_line_height(menu->font)) / 2
                   : abs_y + (ih - 10) / 2;
#else
        int text_y = abs_y + (ih - 10) / 2;
#endif
#ifdef LUI_HAVE_FONTS
        if (menu->font)
            lui_canvas_draw_text(canvas, label_x, text_y, item->label, -1,
                                 menu->font, tc);
        else
#endif
            draw_text_rects(canvas, label_x, text_y,
                            item->label, mr.x + mw - menu->padding_x - 14, tc);

        /* Shortcut text (right-aligned) */
        if (item->shortcut[0] != '\0') {
            int sl = (int)strlen(item->shortcut);
#ifdef LUI_HAVE_FONTS
            int sw = menu->font
                   ? lui_font_measure_text(menu->font, item->shortcut, -1)
                   : sl * 7;
#else
            int sw = sl * 7;
#endif
            int shortcut_x = mr.x + mw - menu->padding_x - 14 - sw;
#ifdef LUI_HAVE_FONTS
            if (menu->font)
                lui_canvas_draw_text(canvas, shortcut_x, text_y,
                                     item->shortcut, -1, menu->font, tc);
            else
#endif
                draw_text_rects(canvas, shortcut_x, text_y,
                                item->shortcut, mr.x + mw - menu->padding_x - 14,
                                tc);
        }

        /* Right arrow for submenu items */
        if (item->type == LUI_MENU_ITEM_SUBMENU) {
            int arrow_cx = mr.x + mw - menu->padding_x - 4;
            int arrow_cy = abs_y + ih / 2;
            draw_right_arrow(canvas, arrow_cx, arrow_cy, 8, tc);
        }
    }
}

/* Find the next/previous non-separator, enabled item for keyboard nav.
 * @dir: +1 for down, -1 for up.  Returns -1 if none found. */
static int menu_nav_next(const lui_menu_t *menu, int current, int dir)
{
    int count = menu->item_count;
    if (count == 0) return -1;

    int idx = current;
    for (int i = 0; i < count; i++) {
        idx += dir;
        if (idx < 0) idx = count - 1;
        else if (idx >= count) idx = 0;

        if (menu->items[idx].type != LUI_MENU_ITEM_SEPARATOR &&
            menu->items[idx].enabled)
            return idx;
    }
    return -1;
}

static int menu_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_menu_t *menu = (lui_menu_t *)w;

    if (!menu->open)
        return 0;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        lvg_rect_t mr = menu_popup_rect(menu);

        if (lvg_rect_contains_point(&mr, mx, my)) {
            int idx = menu_hit_item(menu, mx, my);
            if (idx >= 0 && menu->items[idx].enabled &&
                menu->items[idx].type != LUI_MENU_ITEM_SEPARATOR) {

                /* Toggle checkbox */
                if (menu->items[idx].type == LUI_MENU_ITEM_CHECKBOX)
                    menu->items[idx].checked = !menu->items[idx].checked;

                menu->open = false;
                menu->hovered = -1;
                if (menu->on_item_selected)
                    menu->on_item_selected(idx, menu->on_item_selected_user);
            }
            return 1;
        }

        /* Click outside — close */
        menu->open = false;
        menu->hovered = -1;
        return 0;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int idx = menu_hit_item(menu, mx, my);
        if (idx >= 0 && menu->items[idx].type == LUI_MENU_ITEM_SEPARATOR)
            idx = -1;
        menu->hovered = idx;
        return 1;
    }

    if (event->type == LUI_EVENT_KEY_DOWN) {
        switch (event->data.key.key) {
        case LUI_KEY_ESCAPE:
            menu->open = false;
            menu->hovered = -1;
            return 1;

        case LUI_KEY_UP: {
            int start = (menu->hovered >= 0) ? menu->hovered
                                             : menu->item_count;
            int next = menu_nav_next(menu, start, -1);
            if (next >= 0) menu->hovered = next;
            return 1;
        }

        case LUI_KEY_DOWN: {
            int start = (menu->hovered >= 0) ? menu->hovered : -1;
            int next = menu_nav_next(menu, start, +1);
            if (next >= 0) menu->hovered = next;
            return 1;
        }

        case LUI_KEY_RETURN:
            if (menu->hovered >= 0 && menu->hovered < menu->item_count) {
                const lui_menu_item_t *item = &menu->items[menu->hovered];
                if (item->enabled &&
                    item->type != LUI_MENU_ITEM_SEPARATOR) {

                    int idx = menu->hovered;

                    /* Toggle checkbox */
                    if (menu->items[idx].type == LUI_MENU_ITEM_CHECKBOX)
                        menu->items[idx].checked = !menu->items[idx].checked;

                    menu->open = false;
                    menu->hovered = -1;
                    if (menu->on_item_selected)
                        menu->on_item_selected(idx,
                                               menu->on_item_selected_user);
                }
            }
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

void lui_menu_init(lui_menu_t *menu)
{
    if (!menu) return;

    lui_widget_init(&menu->widget);
    menu->widget.width    = lvg_size_hug(0);
    menu->widget.height   = lvg_size_hug(0);
    menu->widget.measure  = menu_measure;
    menu->widget.draw     = menu_draw;
    menu->widget.on_event = menu_event;
    menu->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    menu->item_count       = 0;
    menu->open             = false;
    menu->hovered          = -1;
    menu->popup_x          = 0;
    menu->popup_y          = 0;

    menu->item_height      = 24;
    menu->separator_height = 9;
    menu->padding_x        = 8;
    menu->bg_color         = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    menu->hover_color      = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    menu->text_color       = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    menu->disabled_color   = LVG_COLOR_RGB(0x60, 0x64, 0x68);
    menu->separator_color  = LVG_COLOR_RGB(0x40, 0x44, 0x48);
    menu->border_color     = LVG_COLOR_RGB(0x40, 0x44, 0x48);
    menu->font             = NULL;

    menu->on_item_selected      = NULL;
    menu->on_item_selected_user = NULL;
}

int lui_menu_add_item(lui_menu_t *menu, const char *label,
                      const char *shortcut)
{
    if (!menu || !label || menu->item_count >= LUI_MENU_MAX_ITEMS)
        return -1;

    int idx = menu->item_count;
    lui_menu_item_t *item = &menu->items[idx];

    item->type = LUI_MENU_ITEM_NORMAL;
    item->enabled = true;
    item->checked = false;
    item->submenu = NULL;

    int ll = (int)strlen(label);
    if (ll > LUI_MENU_MAX_LABEL_LEN) ll = LUI_MENU_MAX_LABEL_LEN;
    memcpy(item->label, label, (size_t)ll);
    item->label[ll] = '\0';

    if (shortcut) {
        int sl = (int)strlen(shortcut);
        if (sl > LUI_MENU_MAX_SHORTCUT_LEN) sl = LUI_MENU_MAX_SHORTCUT_LEN;
        memcpy(item->shortcut, shortcut, (size_t)sl);
        item->shortcut[sl] = '\0';
    } else {
        item->shortcut[0] = '\0';
    }

    menu->item_count++;
    return idx;
}

int lui_menu_add_separator(lui_menu_t *menu)
{
    if (!menu || menu->item_count >= LUI_MENU_MAX_ITEMS)
        return -1;

    int idx = menu->item_count;
    lui_menu_item_t *item = &menu->items[idx];

    item->type = LUI_MENU_ITEM_SEPARATOR;
    item->enabled = false;
    item->checked = false;
    item->submenu = NULL;
    item->label[0] = '\0';
    item->shortcut[0] = '\0';

    menu->item_count++;
    return idx;
}

int lui_menu_add_checkbox(lui_menu_t *menu, const char *label, bool checked)
{
    if (!menu || !label || menu->item_count >= LUI_MENU_MAX_ITEMS)
        return -1;

    int idx = menu->item_count;
    lui_menu_item_t *item = &menu->items[idx];

    item->type = LUI_MENU_ITEM_CHECKBOX;
    item->enabled = true;
    item->checked = checked;
    item->submenu = NULL;

    int ll = (int)strlen(label);
    if (ll > LUI_MENU_MAX_LABEL_LEN) ll = LUI_MENU_MAX_LABEL_LEN;
    memcpy(item->label, label, (size_t)ll);
    item->label[ll] = '\0';
    item->shortcut[0] = '\0';

    menu->item_count++;
    return idx;
}

void lui_menu_set_enabled(lui_menu_t *menu, int index, bool enabled)
{
    if (!menu || index < 0 || index >= menu->item_count)
        return;
    menu->items[index].enabled = enabled;
}

void lui_menu_set_checked(lui_menu_t *menu, int index, bool checked)
{
    if (!menu || index < 0 || index >= menu->item_count)
        return;
    menu->items[index].checked = checked;
}

void lui_menu_popup(lui_menu_t *menu, int x, int y)
{
    if (!menu) return;
    menu->popup_x = x;
    menu->popup_y = y;
    menu->open = true;
    menu->hovered = -1;
}

void lui_menu_close(lui_menu_t *menu)
{
    if (!menu) return;
    menu->open = false;
    menu->hovered = -1;
}

bool lui_menu_is_open(const lui_menu_t *menu)
{
    if (!menu) return false;
    return menu->open;
}
