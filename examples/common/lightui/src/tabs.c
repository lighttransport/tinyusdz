/*
 * tabs.c — Tab strip widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/tabs.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int find_tab_idx(const lui_tabs_t *tabs, int tab_id)
{
    for (int i = 0; i < tabs->tab_count; i++)
        if (tabs->tabs[i].id == tab_id) return i;
    return -1;
}

static int tab_width(const lui_tabs_t *tabs, int index)
{
    int label_len = (int)strlen(tabs->tabs[index].label);
#ifdef LUI_HAVE_FONTS
    int label_w = tabs->font
                ? lui_font_measure_text(tabs->font, tabs->tabs[index].label, -1)
                : label_len * 7;
#else
    int label_w = label_len * 7;
#endif
    int w = tabs->tab_padding * 2 + label_w;
    if (tabs->tabs[index].closable)
        w += tabs->close_size + 4;
    if (w < tabs->tab_min_width) w = tabs->tab_min_width;
    if (w > tabs->tab_max_width) w = tabs->tab_max_width;
    return w;
}

static lvg_rect_t tab_rect(const lui_tabs_t *tabs, int index, lvg_rect_t wr)
{
    int x = wr.x;
    for (int i = 0; i < index; i++)
        x += tab_width(tabs, i);
    return lvg_rect_make(x, wr.y, tab_width(tabs, index), tabs->tab_height);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int tabs_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    const lui_tabs_t *tabs = (const lui_tabs_t *)w;
    (void)user;
    int total_w = 0;
    for (int i = 0; i < tabs->tab_count; i++)
        total_w += tab_width(tabs, i);
    *out_w = total_w > 200 ? total_w : 200;
    *out_h = tabs->tab_height;
    return 0;
}

static void tabs_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_tabs_t *tabs = (lui_tabs_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, wr.height, tabs->bg);

    /* Bottom border */
    lvg_canvas_fill_rect(canvas, wr.x, wr.y + tabs->tab_height - 1,
                          wr.width, 1, tabs->border_color);

    for (int i = 0; i < tabs->tab_count; i++) {
        lui_tab_t *tab = &tabs->tabs[i];
        lvg_rect_t tr = tab_rect(tabs, i, wr);

        /* Tab background */
        lvg_color_t bg;
        if (tab->id == tabs->active_id)
            bg = tabs->tab_active_bg;
        else if (tab->id == tabs->hovered_id)
            bg = tabs->tab_hover_bg;
        else
            bg = tabs->tab_bg;

        lvg_canvas_fill_rounded_rect(canvas, tr.x, tr.y,
                                      tr.width, tr.height + tabs->corner_radius,
                                      tabs->corner_radius, bg);

        /* Active tab covers the bottom border */
        if (tab->id == tabs->active_id)
            lvg_canvas_fill_rect(canvas, tr.x, tr.y + tr.height - 1,
                                  tr.width, 1, bg);

        /* Accent color bar at top */
        if (LVG_COLOR_A(tab->color) > 0)
            lvg_canvas_fill_rect(canvas, tr.x + 2, tr.y,
                                  tr.width - 4, 2, tab->color);

        /* Label */
        lvg_color_t tc = (tab->id == tabs->active_id)
                       ? tabs->text_active : tabs->text_color;
        int tx = tr.x + tabs->tab_padding;
#ifdef LUI_HAVE_FONTS
        if (tabs->font) {
            int ty = tr.y + lui_font_ascent(tabs->font) +
                     (tr.height - lui_font_line_height(tabs->font)) / 2;
            lui_canvas_draw_text(canvas, tx, ty, tab->label, -1,
                                 tabs->font, tc);
        } else {
#endif
            int label_len = (int)strlen(tab->label);
            int ty = tr.y + (tr.height - 10) / 2;
            for (int c = 0; c < label_len && tx < tr.x + tr.width - tabs->tab_padding; c++) {
                lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, tc);
                tx += 7;
            }
#ifdef LUI_HAVE_FONTS
        }
#endif

        /* Close button */
        if (tab->closable) {
            int cs = tabs->close_size;
            int cx = tr.x + tr.width - tabs->tab_padding - cs;
            int cy = tr.y + (tr.height - cs) / 2;
            lvg_color_t cc = (tab->id == tabs->close_hovered_id)
                           ? tabs->close_hover : tabs->close_color;
            /* X mark */
            lvg_canvas_draw_line(canvas, cx + 2, cy + 2,
                                  cx + cs - 2, cy + cs - 2, cc, 1);
            lvg_canvas_draw_line(canvas, cx + cs - 2, cy + 2,
                                  cx + 2, cy + cs - 2, cc, 1);
        }
    }
}

static int tabs_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_tabs_t *tabs = (lui_tabs_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        tabs->hovered_id = -1;
        tabs->close_hovered_id = -1;
        for (int i = 0; i < tabs->tab_count; i++) {
            lvg_rect_t tr = tab_rect(tabs, i, wr);
            if (lvg_rect_contains_point(&tr, mx, my)) {
                tabs->hovered_id = tabs->tabs[i].id;
                /* Check close button hover */
                if (tabs->tabs[i].closable) {
                    int cs = tabs->close_size;
                    int cx = tr.x + tr.width - tabs->tab_padding - cs;
                    int cy = tr.y + (tr.height - cs) / 2;
                    if (mx >= cx && mx < cx + cs && my >= cy && my < cy + cs)
                        tabs->close_hovered_id = tabs->tabs[i].id;
                }
                break;
            }
        }
        return tabs->hovered_id >= 0 ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        for (int i = 0; i < tabs->tab_count; i++) {
            lvg_rect_t tr = tab_rect(tabs, i, wr);
            if (!lvg_rect_contains_point(&tr, mx, my)) continue;

            /* Check close button */
            if (tabs->tabs[i].closable) {
                int cs = tabs->close_size;
                int cx = tr.x + tr.width - tabs->tab_padding - cs;
                int cy = tr.y + (tr.height - cs) / 2;
                if (mx >= cx && mx < cx + cs && my >= cy && my < cy + cs) {
                    if (tabs->on_close)
                        tabs->on_close(tabs->tabs[i].id, tabs->on_close_user);
                    return 1;
                }
            }

            /* Select tab */
            int old = tabs->active_id;
            tabs->active_id = tabs->tabs[i].id;
            if (old != tabs->active_id && tabs->on_change)
                tabs->on_change(tabs->active_id, tabs->on_change_user);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_tabs_init(lui_tabs_t *tabs)
{
    if (!tabs) return;

    lui_widget_init(&tabs->widget);
    tabs->widget.width   = lvg_size_fill(1);
    tabs->widget.height  = lvg_size_hug(0);
    tabs->widget.measure = tabs_measure;
    tabs->widget.draw    = tabs_draw;
    tabs->widget.on_event = tabs_event;

    tabs->tab_count  = 0;
    tabs->next_id    = 1;
    tabs->active_id  = -1;
    tabs->hovered_id = -1;
    tabs->close_hovered_id = -1;

    tabs->tab_height    = 28;
    tabs->tab_min_width = 60;
    tabs->tab_max_width = 180;
    tabs->tab_padding   = 12;
    tabs->close_size    = 12;
    tabs->corner_radius = 4;

    tabs->bg            = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    tabs->tab_bg        = LVG_COLOR_RGB(0x28, 0x2C, 0x34);
    tabs->tab_active_bg = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    tabs->tab_hover_bg  = LVG_COLOR_RGB(0x34, 0x38, 0x40);
    tabs->text_color    = LVG_COLOR_RGB(0x90, 0x94, 0x9A);
    tabs->text_active   = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    tabs->close_color   = LVG_COLOR_RGB(0x70, 0x74, 0x7A);
    tabs->close_hover   = LVG_COLOR_RGB(0xE0, 0x50, 0x50);
    tabs->border_color  = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    tabs->font          = NULL;

    tabs->on_change      = NULL;
    tabs->on_change_user = NULL;
    tabs->on_close       = NULL;
    tabs->on_close_user  = NULL;
}

int lui_tabs_add(lui_tabs_t *tabs, const char *label, bool closable)
{
    if (!tabs || !label || tabs->tab_count >= LUI_TABS_MAX) return -1;

    lui_tab_t *tab = &tabs->tabs[tabs->tab_count];
    tab->id = tabs->next_id++;
    tab->color = 0;
    tab->closable = closable;

    int len = (int)strlen(label);
    if (len > LUI_TAB_MAX_LABEL) len = LUI_TAB_MAX_LABEL;
    memcpy(tab->label, label, len);
    tab->label[len] = '\0';

    tabs->tab_count++;

    /* Auto-activate first tab */
    if (tabs->active_id < 0)
        tabs->active_id = tab->id;

    return tab->id;
}

void lui_tabs_remove(lui_tabs_t *tabs, int tab_id)
{
    if (!tabs) return;
    int idx = find_tab_idx(tabs, tab_id);
    if (idx < 0) return;

    for (int i = idx; i < tabs->tab_count - 1; i++)
        tabs->tabs[i] = tabs->tabs[i + 1];
    tabs->tab_count--;

    if (tabs->active_id == tab_id) {
        if (tabs->tab_count > 0)
            tabs->active_id = tabs->tabs[idx < tabs->tab_count ? idx : tabs->tab_count - 1].id;
        else
            tabs->active_id = -1;
    }
}

void lui_tabs_set_active(lui_tabs_t *tabs, int tab_id)
{
    if (!tabs) return;
    tabs->active_id = tab_id;
}

int lui_tabs_get_active(const lui_tabs_t *tabs)
{
    return tabs ? tabs->active_id : -1;
}

lui_tab_t *lui_tabs_get(lui_tabs_t *tabs, int tab_id)
{
    if (!tabs) return NULL;
    int idx = find_tab_idx(tabs, tab_id);
    return idx >= 0 ? &tabs->tabs[idx] : NULL;
}
